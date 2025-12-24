#pragma once
// =============================================================================
// FIXSession.hpp - FIX 4.4 Session Management for cTrader
// =============================================================================
// CHIMERA HFT - Complete FIX Session Layer
// Handles: Logon, Logout, Heartbeat, Sequence Numbers, Resend Requests
// =============================================================================

#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "FIXConfig.hpp"
#include "FIXMessage.hpp"
#include "FIXSSLTransport.hpp"
#include "FIXResendRing.hpp"

namespace Chimera {

// =============================================================================
// FIX SESSION STATE
// =============================================================================
enum class FIXSessionState {
    DISCONNECTED,
    CONNECTING,
    LOGON_SENT,
    LOGGED_ON,
    LOGOUT_SENT,
    DISCONNECTING
};

inline const char* toString(FIXSessionState state) {
    switch (state) {
        case FIXSessionState::DISCONNECTED:   return "DISCONNECTED";
        case FIXSessionState::CONNECTING:     return "CONNECTING";
        case FIXSessionState::LOGON_SENT:     return "LOGON_SENT";
        case FIXSessionState::LOGGED_ON:      return "LOGGED_ON";
        case FIXSessionState::LOGOUT_SENT:    return "LOGOUT_SENT";
        case FIXSessionState::DISCONNECTING:  return "DISCONNECTING";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// FIX SESSION CALLBACKS
// =============================================================================
using FIXLogonCallback = std::function<void()>;
using FIXLogoutCallback = std::function<void(const std::string& reason)>;
using FIXMessageCallback = std::function<void(const FIXMessage& msg)>;
using FIXRejectCallback = std::function<void(int refSeqNum, int rejectCode, const std::string& text)>;

// =============================================================================
// FIX SESSION CLASS
// =============================================================================
class FIXSession {
public:
    FIXSession(const std::string& sessionName)
        : sessionName_(sessionName)
        , state_(FIXSessionState::DISCONNECTED)
        , outSeqNum_(1)
        , inSeqNum_(1)
        , heartbeatRunning_(false)
        , lastSendTime_(std::chrono::steady_clock::now())
        , lastRecvTime_(std::chrono::steady_clock::now())
        , testReqID_(0)
        , testReqPending_(false)
    {}
    
    ~FIXSession() {
        stop();
    }
    
    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    
    void setConfig(const FIXConfig& cfg) {
        config_ = cfg;
    }
    
    void setSenderSubID(const std::string& subID) {
        senderSubID_ = subID;
    }
    
    void setOnLogon(FIXLogonCallback cb) { onLogon_ = std::move(cb); }
    void setOnLogout(FIXLogoutCallback cb) { onLogout_ = std::move(cb); }
    void setOnMessage(FIXMessageCallback cb) { onMessage_ = std::move(cb); }
    void setOnReject(FIXRejectCallback cb) { onReject_ = std::move(cb); }
    
    // =========================================================================
    // CONNECTION LIFECYCLE
    // =========================================================================
    
    bool start(const std::string& host, int port) {
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] START host=%s port=%d\n", 
            sessionName_.c_str(), host.c_str(), port);
        fflush(stderr);
        
        if (state_.load() != FIXSessionState::DISCONNECTED) {
            fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] ERROR: state=%d not DISCONNECTED\n",
                sessionName_.c_str(), (int)state_.load());
            fflush(stderr);
            return false;
        }
        
        state_.store(FIXSessionState::CONNECTING);
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] state=CONNECTING\n", sessionName_.c_str());
        fflush(stderr);
        
        // Setup transport callbacks
        transport_.setRxCallback([this](const std::string& msg) {
            onRawMessage(msg);
        });
        
        transport_.setStateCallback([this](bool connected) {
            fprintf(stderr, "[FIX-DBG][FIXSession::stateCallback][%s] connected=%d\n",
                sessionName_.c_str(), connected ? 1 : 0);
            fflush(stderr);
            if (!connected && state_.load() != FIXSessionState::DISCONNECTED) {
                std::cerr << "[" << sessionName_ << "] Connection lost\n";
                state_.store(FIXSessionState::DISCONNECTED);
                if (onLogout_) {
                    onLogout_("Connection lost");
                }
            }
        });
        
        // Connect transport
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] Calling transport_.connect()...\n", sessionName_.c_str());
        fflush(stderr);
        
        if (!transport_.connect(host, port)) {
            fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] transport_.connect() FAILED\n", sessionName_.c_str());
            fflush(stderr);
            state_.store(FIXSessionState::DISCONNECTED);
            return false;
        }
        
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] transport_.connect() OK\n", sessionName_.c_str());
        fflush(stderr);
        
        // Start heartbeat thread
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] Creating heartbeat thread...\n", sessionName_.c_str());
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] heartbeatThread_.joinable()=%d BEFORE\n", 
            sessionName_.c_str(), heartbeatThread_.joinable() ? 1 : 0);
        fflush(stderr);
        
        heartbeatRunning_.store(true);
        
        try {
            heartbeatThread_ = std::thread(&FIXSession::heartbeatLoop, this);
            fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] heartbeat thread CREATED\n", sessionName_.c_str());
            fflush(stderr);
        } catch (const std::exception& e) {
            fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] !!! EXCEPTION creating heartbeat: %s\n", 
                sessionName_.c_str(), e.what());
            fflush(stderr);
            transport_.disconnect();
            state_.store(FIXSessionState::DISCONNECTED);
            return false;
        }
        
        // =====================================================================
        // CRITICAL FIX: Wait for RX thread to be ready BEFORE sending LOGON
        // This prevents the race condition where server ACK arrives before
        // RX thread is blocking on SSL_read
        // =====================================================================
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] Waiting for RX thread ready...\n", sessionName_.c_str());
        fflush(stderr);
        
        if (!transport_.waitForRxReady(5000)) {
            fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] ERROR: RX thread not ready after 5s!\n", 
                sessionName_.c_str());
            fflush(stderr);
            transport_.disconnect();
            state_.store(FIXSessionState::DISCONNECTED);
            return false;
        }
        
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] RX thread ready, sending logon...\n", sessionName_.c_str());
        fflush(stderr);
        
        // Now safe to send LOGON - RX is guaranteed to catch the ACK
        sendLogon(true);
        
        fprintf(stderr, "[FIX-DBG][FIXSession::start][%s] COMPLETE SUCCESS\n", sessionName_.c_str());
        fflush(stderr);
        return true;
    }
    
    void stop() {
        fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] START state=%d\n", 
            sessionName_.c_str(), (int)state_.load());
        fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] heartbeatThread_.joinable()=%d\n",
            sessionName_.c_str(), heartbeatThread_.joinable() ? 1 : 0);
        fflush(stderr);
        
        FIXSessionState expected = state_.load();
        if (expected == FIXSessionState::DISCONNECTED) {
            fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] Already DISCONNECTED, returning\n", sessionName_.c_str());
            fflush(stderr);
            return;
        }
        
        // Send LOGOUT if we're logged on (graceful disconnect)
        if (expected == FIXSessionState::LOGGED_ON || expected == FIXSessionState::LOGON_SENT) {
            fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] Sending LOGOUT...\n", sessionName_.c_str());
            fflush(stderr);
            try {
                sendLogout("Client disconnect");
                // Give server time to process logout
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            } catch (...) {
                // Ignore errors - we're disconnecting anyway
            }
        }
        
        state_.store(FIXSessionState::DISCONNECTING);
        
        // Stop heartbeat
        heartbeatRunning_.store(false);
        if (heartbeatThread_.joinable()) {
            fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] Joining heartbeat thread...\n", sessionName_.c_str());
            fflush(stderr);
            heartbeatThread_.join();
            fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] heartbeat thread joined\n", sessionName_.c_str());
            fflush(stderr);
        }
        
        // Disconnect transport
        fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] Calling transport_.disconnect()...\n", sessionName_.c_str());
        fflush(stderr);
        transport_.disconnect();
        fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] transport_.disconnect() done\n", sessionName_.c_str());
        fflush(stderr);
        
        state_.store(FIXSessionState::DISCONNECTED);
        
        fprintf(stderr, "[FIX-DBG][FIXSession::stop][%s] COMPLETE\n", sessionName_.c_str());
        fflush(stderr);
    }
    
    // =========================================================================
    // MESSAGE SENDING
    // =========================================================================
    
    bool sendMessage(FIXMessage& msg) {
        if (state_.load() != FIXSessionState::LOGGED_ON) {
            std::cerr << "[" << sessionName_ << "] Cannot send: not logged on\n";
            return false;
        }
        
        return sendRawMessage(msg);
    }
    
    bool sendMarketDataRequest(const std::string& securityId, bool subscribe = true) {
        if (state_.load() != FIXSessionState::LOGGED_ON) return false;
        
        std::string mdReqID = generateClOrdID();
        // Use SecurityID (tag 48) for cTrader instead of Symbol (tag 55)
        std::string raw = buildMarketDataRequestMessage(
            config_, getNextOutSeqNum(), senderSubID_,
            mdReqID, securityId, 1, subscribe, true);  // useSecurityId=true
        
        return sendRawString(raw);
    }
    
    // Send SecurityListRequest (35=x) to get symbol→SecurityID mapping
    bool sendSecurityListRequest() {
        if (state_.load() != FIXSessionState::LOGGED_ON) return false;
        
        std::string reqID = "SECLIST_" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        std::string raw = buildSecurityListRequestMessage(
            config_, getNextOutSeqNum(), senderSubID_, reqID);
        
        std::cout << "[" << sessionName_ << "] Sending SecurityListRequest (ID=" << reqID << ")\n";
        return sendRawString(raw);
    }
    
    // v6.88 FIX: Added positionEffect parameter - REQUIRED for cTrader CFDs
    bool sendNewOrder(const std::string& symbol, char side, double qty, 
                      char ordType = FIXOrdType::Market, double price = 0.0,
                      char positionEffect = FIXPositionEffect::Open) {
        if (state_.load() != FIXSessionState::LOGGED_ON) {
            std::cout << "[" << sessionName_ << "] ORDER REJECTED - Not logged on\n";
            return false;
        }
        
        std::string clOrdID = generateClOrdID();
        std::string raw = buildNewOrderSingleMessage(
            config_, getNextOutSeqNum(), senderSubID_,
            clOrdID, symbol, side, qty, ordType, price, 
            FIXTimeInForce::IOC, positionEffect);
        
        // v6.88: Log the full FIX message for debugging
        std::cout << "\n[FIX_ORDER_DEBUG] " << symbol 
                  << " side=" << side 
                  << " qty=" << qty 
                  << " posEffect=" << positionEffect << "\n";
        printMessage("TX_ORDER", raw);
        
        return sendRawString(raw);
    }
    
    // =========================================================================
    // STATE QUERIES
    // =========================================================================
    
    FIXSessionState getState() const { return state_.load(); }
    bool isLoggedOn() const { return state_.load() == FIXSessionState::LOGGED_ON; }
    uint32_t getOutSeqNum() const { return outSeqNum_.load(); }
    uint32_t getInSeqNum() const { return inSeqNum_.load(); }
    
    const FIXSSLTransport& getTransport() const { return transport_; }
    
private:
    // =========================================================================
    // LOGON/LOGOUT
    // =========================================================================
    
    void sendLogon(bool resetSeqNum = false) {
        if (resetSeqNum) {
            outSeqNum_.store(1);
            inSeqNum_.store(1);
        }
        
        std::string raw = buildLogonMessage(config_, getNextOutSeqNum(), senderSubID_, resetSeqNum);
        
        state_.store(FIXSessionState::LOGON_SENT);
        std::cout << "[" << sessionName_ << "] Sending LOGON (seq=" << (outSeqNum_.load() - 1) << ")\n";
        
        // Debug: print message
        printMessage("TX", raw);
        
        transport_.sendRaw(raw);
        updateSendTime();
    }
    
    void sendLogout(const std::string& text = "") {
        std::string raw = buildLogoutMessage(config_, getNextOutSeqNum(), senderSubID_, text);
        state_.store(FIXSessionState::LOGOUT_SENT);
        std::cout << "[" << sessionName_ << "] Sending LOGOUT\n";
        transport_.sendRaw(raw);
        updateSendTime();
    }
    
    // =========================================================================
    // HEARTBEAT
    // =========================================================================
    
    void sendHeartbeat(const std::string& testReqID = "") {
        std::string raw = buildHeartbeatMessage(config_, getNextOutSeqNum(), senderSubID_, testReqID);
        transport_.sendRaw(raw);
        updateSendTime();
    }
    
    void sendTestRequest() {
        std::string testReqID = std::to_string(++testReqID_);
        std::string raw = buildTestRequestMessage(config_, getNextOutSeqNum(), senderSubID_, testReqID);
        testReqPending_.store(true);
        transport_.sendRaw(raw);
        updateSendTime();
    }
    
    void heartbeatLoop() {
        while (heartbeatRunning_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            if (state_.load() != FIXSessionState::LOGGED_ON) continue;
            
            auto now = std::chrono::steady_clock::now();
            auto lastSend = lastSendTime_.load();
            auto lastRecv = lastRecvTime_.load();
            
            auto sendElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastSend).count();
            auto recvElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastRecv).count();
            
            // Send heartbeat if we haven't sent anything
            if (sendElapsed >= config_.heartbeatIntervalSec) {
                sendHeartbeat();
            }
            
            // Send test request if we haven't received anything
            if (recvElapsed >= config_.heartbeatIntervalSec + 5) {
                if (!testReqPending_.load()) {
                    std::cout << "[" << sessionName_ << "] No data received, sending TestRequest\n";
                    sendTestRequest();
                } else if (recvElapsed >= config_.heartbeatIntervalSec * 2) {
                    // Connection dead
                    std::cerr << "[" << sessionName_ << "] Connection timeout, disconnecting\n";
                    transport_.disconnect();
                }
            }
        }
    }
    
    // =========================================================================
    // MESSAGE HANDLING
    // =========================================================================
    
    void onRawMessage(const std::string& raw) {
        updateRecvTime();
        
        FIXMessage msg;
        if (!msg.parseZeroCopy(raw.data(), raw.length())) {
            std::cerr << "[" << sessionName_ << "] Failed to parse FIX message\n";
            return;
        }
        
        // Debug: print message
        printMessage("RX", raw);
        
        // Check sequence number
        int recvSeqNum = msg.getIntFast(FIXTag::MsgSeqNum);
        int expectedSeq = inSeqNum_.load();
        
        if (recvSeqNum > expectedSeq) {
            std::cout << "[" << sessionName_ << "] Sequence gap: expected " << expectedSeq 
                      << ", got " << recvSeqNum << "\n";
            // TODO: Send ResendRequest
        }
        
        if (recvSeqNum >= expectedSeq) {
            inSeqNum_.store(recvSeqNum + 1);
        }
        
        // Handle by message type
        char msgType = msg.getMsgType();
        
        switch (msgType) {
            case FIXMsgType::Logon:
                handleLogon(msg);
                break;
                
            case FIXMsgType::Logout:
                handleLogout(msg);
                break;
                
            case FIXMsgType::Heartbeat:
                handleHeartbeat(msg);
                break;
                
            case FIXMsgType::TestRequest:
                handleTestRequest(msg);
                break;
                
            case FIXMsgType::Reject:
                handleReject(msg);
                break;
                
            case FIXMsgType::ResendRequest:
                handleResendRequest(msg);
                break;
                
            case FIXMsgType::SequenceReset:
                handleSequenceReset(msg);
                break;
                
            default:
                // Application message
                if (onMessage_) {
                    onMessage_(msg);
                }
                break;
        }
    }
    
    void handleLogon(const FIXMessage& /*msg*/) {
        std::cout << "[" << sessionName_ << "] LOGON received\n";
        state_.store(FIXSessionState::LOGGED_ON);
        if (onLogon_) {
            onLogon_();
        }
    }
    
    void handleLogout(const FIXMessage& msg) {
        std::string text = msg.getString(FIXTag::Text);
        std::cout << "[" << sessionName_ << "] LOGOUT received: " << text << "\n";
        state_.store(FIXSessionState::DISCONNECTED);
        if (onLogout_) {
            onLogout_(text);
        }
    }
    
    void handleHeartbeat(const FIXMessage& /*msg*/) {
        testReqPending_.store(false);
    }
    
    void handleTestRequest(const FIXMessage& msg) {
        std::string testReqID = msg.getString(FIXTag::TestReqID);
        sendHeartbeat(testReqID);
    }
    
    void handleReject(const FIXMessage& msg) {
        int refSeqNum = msg.getIntFast(FIXTag::RefSeqNum);
        int rejectCode = msg.getIntFast(373);  // SessionRejectReason
        std::string text = msg.getString(FIXTag::Text);
        
        std::cerr << "[" << sessionName_ << "] REJECT: refSeq=" << refSeqNum 
                  << ", code=" << rejectCode << ", text=" << text << "\n";
        
        if (onReject_) {
            onReject_(refSeqNum, rejectCode, text);
        }
    }
    
    void handleResendRequest(const FIXMessage& msg) {
        int beginSeq = msg.getIntFast(FIXTag::BeginSeqNo);
        int endSeq = msg.getIntFast(FIXTag::EndSeqNo);
        
        std::cout << "[" << sessionName_ << "] ResendRequest: " << beginSeq << " to " << endSeq << "\n";
        
        // For now, send SequenceReset-GapFill
        FIXMessage reset;
        reset.setMsgType(FIXMsgType::SequenceReset);
        reset.setSendingTime();
        reset.setField(123, 'Y');  // GapFillFlag
        reset.setField(36, static_cast<int>(outSeqNum_.load()));  // NewSeqNo
        
        std::string raw = reset.encode(config_.senderCompID, config_.targetCompID, 
                                        beginSeq, senderSubID_);
        transport_.sendRaw(raw);
    }
    
    void handleSequenceReset(const FIXMessage& msg) {
        int newSeqNo = msg.getIntFast(36);  // NewSeqNo
        std::cout << "[" << sessionName_ << "] SequenceReset to " << newSeqNo << "\n";
        inSeqNum_.store(newSeqNo);
    }
    
    // =========================================================================
    // UTILITIES
    // =========================================================================
    
    uint32_t getNextOutSeqNum() {
        return outSeqNum_.fetch_add(1);
    }
    
    bool sendRawMessage(FIXMessage& msg) {
        std::string raw = msg.encode(config_.senderCompID, config_.targetCompID, 
                                      getNextOutSeqNum(), senderSubID_);
        return sendRawString(raw);
    }
    
    bool sendRawString(const std::string& raw) {
        // Store in resend buffer
        resendRing_.store(outSeqNum_.load() - 1, raw.data(), raw.length());
        
        // Send
        bool ok = transport_.sendRaw(raw);
        if (ok) {
            updateSendTime();
        }
        return ok;
    }
    
    void updateSendTime() {
        lastSendTime_.store(std::chrono::steady_clock::now());
    }
    
    void updateRecvTime() {
        lastRecvTime_.store(std::chrono::steady_clock::now());
    }
    
    std::string generateClOrdID() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        std::ostringstream oss;
        oss << sessionName_ << "_" << ms << "_" << counter.fetch_add(1);
        return oss.str();
    }
    
    void printMessage(const char* dir, const std::string& raw) {
        // Replace SOH with | for display
        std::string display = raw;
        for (char& c : display) {
            if (c == '\x01') c = '|';
        }
        std::cout << "[" << sessionName_ << "] " << dir << ": " << display << "\n";
    }
    
private:
    std::string sessionName_;
    std::string senderSubID_;
    FIXConfig config_;
    
    FIXSSLTransport transport_;
    FIXResendRing resendRing_;
    
    std::atomic<FIXSessionState> state_;
    std::atomic<uint32_t> outSeqNum_;
    std::atomic<uint32_t> inSeqNum_;
    
    std::thread heartbeatThread_;
    std::atomic<bool> heartbeatRunning_;
    
    std::atomic<std::chrono::steady_clock::time_point> lastSendTime_;
    std::atomic<std::chrono::steady_clock::time_point> lastRecvTime_;
    
    std::atomic<uint64_t> testReqID_;
    std::atomic<bool> testReqPending_;
    
    FIXLogonCallback onLogon_;
    FIXLogoutCallback onLogout_;
    FIXMessageCallback onMessage_;
    FIXRejectCallback onReject_;
};

} // namespace Chimera
