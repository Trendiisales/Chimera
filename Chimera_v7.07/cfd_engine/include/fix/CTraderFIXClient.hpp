#pragma once
// =============================================================================
// CTraderFIXClient.hpp - Complete cTrader FIX Client for BlackBull Markets
// =============================================================================
// CHIMERA HFT - Dual Session FIX Client
// TRADE Session: Port 5212 - MUST CONNECT FIRST (authentication/authority)
// QUOTE Session: Port 5211 - Market Data (subordinate to TRADE)
// =============================================================================
// CRITICAL FIX (2024-12-22):
// cTrader REQUIRES TRADE session to log in FIRST before QUOTE.
// QUOTE logon is IGNORED if TRADE is not already logged in.
// This is undocumented but enforced by cTrader servers.
// =============================================================================

#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <map>
#include <iostream>

#include "FIXConfig.hpp"
#include "FIXSession.hpp"
#include "FIXMessage.hpp"

namespace Chimera {

// =============================================================================
// CONNECTION STATE MACHINE
// =============================================================================
// Valid lifecycle (non-negotiable):
// DISCONNECTED -> CONNECTING_TRADE -> TRADE_ACTIVE -> CONNECTING_QUOTE -> RUNNING
//
// Failure rules:
// - If TRADE fails -> reset everything
// - If QUOTE fails -> keep TRADE, retry QUOTE
// - If TRADE disconnects -> force QUOTE disconnect
// - Never retry QUOTE unless TRADE is active
// =============================================================================
enum class CTraderState {
    DISCONNECTED,
    CONNECTING_TRADE,
    TRADE_ACTIVE,
    CONNECTING_QUOTE,
    RUNNING,
    RECONNECTING
};

inline const char* toString(CTraderState s) {
    switch (s) {
        case CTraderState::DISCONNECTED:      return "DISCONNECTED";
        case CTraderState::CONNECTING_TRADE:  return "CONNECTING_TRADE";
        case CTraderState::TRADE_ACTIVE:      return "TRADE_ACTIVE";
        case CTraderState::CONNECTING_QUOTE:  return "CONNECTING_QUOTE";
        case CTraderState::RUNNING:           return "RUNNING";
        case CTraderState::RECONNECTING:      return "RECONNECTING";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// MARKET DATA TICK
// =============================================================================
struct CTraderTick {
    std::string symbol;
    double bid;
    double ask;
    double bidSize;
    double askSize;
    uint64_t timestamp;
    
    double mid() const { return (bid + ask) / 2.0; }
    double spread() const { return ask - bid; }
};

// =============================================================================
// EXECUTION REPORT
// =============================================================================
struct CTraderExecReport {
    std::string symbol;
    std::string clOrdID;
    std::string orderID;
    std::string execID;
    char execType;
    char ordStatus;
    char side;
    double orderQty;
    double cumQty;
    double leavesQty;
    double avgPx;
    double lastPx;
    double lastQty;
    std::string text;
    uint64_t timestamp;
    
    bool isFill() const { return execType == FIXExecType::Fill || execType == FIXExecType::PartialFill; }
    bool isNew() const { return execType == FIXExecType::New; }
    bool isReject() const { return execType == FIXExecType::Rejected; }
    bool isCancel() const { return execType == FIXExecType::Canceled; }
};

// =============================================================================
// CALLBACKS
// =============================================================================
using CTraderTickCallback = std::function<void(const CTraderTick&)>;
using CTraderExecCallback = std::function<void(const CTraderExecReport&)>;
using CTraderStateCallback = std::function<void(bool quoteConnected, bool tradeConnected)>;

// =============================================================================
// CTRADER FIX CLIENT
// =============================================================================
class CTraderFIXClient {
public:
    CTraderFIXClient()
        : quoteSession_("QUOTE")
        , tradeSession_("TRADE")
        , quoteConnected_(false)
        , tradeConnected_(false)
        , shutdown_(false)
        , externalRunning_(nullptr)
        , state_(CTraderState::DISCONNECTED)
        , tickCount_(0)
        , firstTickTime_(0)
    {}
    
    ~CTraderFIXClient() {
        disconnect();
    }
    
    // Set external running flag for coordinated shutdown
    void setExternalRunning(std::atomic<bool>* running) {
        externalRunning_ = running;
    }
    
    // Check if we should stop (either internal shutdown or external running=false)
    bool shouldStop() const {
        if (shutdown_.load()) return true;
        if (externalRunning_ && !externalRunning_->load()) return true;
        return false;
    }
    
    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    
    void setConfig(const FIXConfig& cfg) {
        config_ = cfg;
        
        // Configure TRADE session (primary - connects first!)
        tradeSession_.setConfig(cfg);
        tradeSession_.setSenderSubID(cfg.senderSubID_Trade);
        
        // Configure QUOTE session (subordinate - connects after TRADE)
        quoteSession_.setConfig(cfg);
        quoteSession_.setSenderSubID(cfg.senderSubID_Quote);
    }
    
    void setOnTick(CTraderTickCallback cb) { onTick_ = std::move(cb); }
    void setOnExec(CTraderExecCallback cb) { onExec_ = std::move(cb); }
    void setOnState(CTraderStateCallback cb) { onState_ = std::move(cb); }
    
    // =========================================================================
    // CONNECTION - TRADE FIRST, THEN QUOTE
    // =========================================================================
    // cTrader FIX Contract (undocumented but enforced):
    // 1. TRADE must log in first (provides authentication authority)
    // 2. QUOTE is subordinate (ignored unless TRADE is active)
    // 3. If TRADE drops -> QUOTE must be dropped too
    // 4. QUOTE cannot exist alone
    // =========================================================================
    
    bool connect() {
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] >>> START\n");
        fflush(stderr);
        
        // CRITICAL: Ensure clean state before attempting connection
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Calling disconnect() first...\n");
        fflush(stderr);
        disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] disconnect() done, proceeding...\n");
        fflush(stderr);
        
        std::cout << "[CTraderFIX] ============================================\n";
        std::cout << "[CTraderFIX] CONNECTING TO CTRADER FIX\n";
        std::cout << "[CTraderFIX] Host: " << config_.host << "\n";
        std::cout << "[CTraderFIX] TRADE port: " << config_.tradePort << " (connects FIRST)\n";
        std::cout << "[CTraderFIX] QUOTE port: " << config_.pricePort << " (connects AFTER trade)\n";
        std::cout << "[CTraderFIX] SenderCompID: " << config_.senderCompID << "\n";
        std::cout << "[CTraderFIX] ============================================\n";
        
        // Setup TRADE session callbacks (PRIMARY SESSION)
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Setting up TRADE callbacks...\n");
        fflush(stderr);
        
        tradeSession_.setOnLogon([this]() {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient] TRADE onLogon callback\n");
            fflush(stderr);
            std::cout << "[CTraderFIX] *** TRADE session logged on ***\n";
            tradeConnected_.store(true);
            state_.store(CTraderState::TRADE_ACTIVE);
            notifyState();
        });
        
        tradeSession_.setOnLogout([this](const std::string& reason) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient] TRADE onLogout callback: %s\n", reason.c_str());
            fflush(stderr);
            std::cout << "[CTraderFIX] *** TRADE session logged out: " << reason << " ***\n";
            tradeConnected_.store(false);
            
            if (quoteConnected_.load()) {
                std::cout << "[CTraderFIX] QUOTE forced down (TRADE lost)\n";
                quoteSession_.stop();
                quoteConnected_.store(false);
            }
            
            state_.store(CTraderState::DISCONNECTED);
            notifyState();
        });
        
        tradeSession_.setOnMessage([this](const FIXMessage& msg) {
            handleTradeMessage(msg);
        });
        
        // Setup QUOTE session callbacks (SUBORDINATE SESSION)
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Setting up QUOTE callbacks...\n");
        fflush(stderr);
        
        quoteSession_.setOnLogon([this]() {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient] QUOTE onLogon callback\n");
            fflush(stderr);
            std::cout << "[CTraderFIX] *** QUOTE session logged on ***\n";
            quoteConnected_.store(true);
            state_.store(CTraderState::RUNNING);
            notifyState();
        });
        
        quoteSession_.setOnLogout([this](const std::string& reason) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient] QUOTE onLogout callback: %s\n", reason.c_str());
            fflush(stderr);
            std::cout << "[CTraderFIX] QUOTE session logged out: " << reason << "\n";
            quoteConnected_.store(false);
            
            if (tradeConnected_.load()) {
                state_.store(CTraderState::TRADE_ACTIVE);
            } else {
                state_.store(CTraderState::DISCONNECTED);
            }
            notifyState();
        });
        
        quoteSession_.setOnMessage([this](const FIXMessage& msg) {
            handleQuoteMessage(msg);
        });
        
        // =====================================================================
        // STEP 1: Connect TRADE session FIRST (port 5212)
        // =====================================================================
        state_.store(CTraderState::CONNECTING_TRADE);
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] >>> STEP 1: Starting TRADE session...\n");
        fflush(stderr);
        std::cout << "[CTraderFIX] STEP 1: Starting TRADE session on port " << config_.tradePort << "...\n";
        
        if (!tradeSession_.start(config_.host, config_.tradePort)) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] TRADE session start FAILED\n");
            fflush(stderr);
            std::cerr << "[CTraderFIX] FATAL: Failed to start TRADE session\n";
            state_.store(CTraderState::DISCONNECTED);
            return false;
        }
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] TRADE session started OK\n");
        fflush(stderr);
        
        // Wait for TRADE logon (30 seconds max, interruptible)
        // Demo FIX servers are slow - 10s is too aggressive
        std::cout << "[CTraderFIX] Waiting for TRADE logon...\n";
        for (int i = 0; i < 300 && !tradeConnected_.load() && !shouldStop(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (i > 0 && i % 50 == 0) {
                std::cout << "[CTraderFIX] Still waiting for TRADE logon... (" << (i/10) << "s)\n";
            }
        }
        
        if (shouldStop()) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Shutdown during TRADE logon\n");
            fflush(stderr);
            std::cout << "[CTraderFIX] Shutdown requested during TRADE logon\n";
            tradeSession_.stop();
            state_.store(CTraderState::DISCONNECTED);
            return false;
        }
        
        if (!tradeConnected_.load()) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] TRADE logon TIMEOUT\n");
            fflush(stderr);
            std::cerr << "[CTraderFIX] FATAL: TRADE session logon timeout (30s)\n";
            tradeSession_.stop();
            state_.store(CTraderState::DISCONNECTED);
            return false;
        }
        
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] TRADE logon SUCCESS\n");
        fflush(stderr);
        std::cout << "[CTraderFIX] TRADE logon OK - proceeding to QUOTE\n";
        
        // =====================================================================
        // STEP 2: Connect QUOTE session AFTER TRADE is active (port 5211)
        // =====================================================================
        state_.store(CTraderState::CONNECTING_QUOTE);
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] >>> STEP 2: Starting QUOTE session...\n");
        fflush(stderr);
        std::cout << "[CTraderFIX] STEP 2: Starting QUOTE session on port " << config_.pricePort << "...\n";
        std::cout << "[CTraderFIX] (QUOTE waiting for TRADE - contract satisfied)\n";
        
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Calling quoteSession_.start()...\n");
        fflush(stderr);
        
        if (!quoteSession_.start(config_.host, config_.pricePort)) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] quoteSession_.start() FAILED\n");
            fflush(stderr);
            std::cerr << "[CTraderFIX] FATAL: Failed to start QUOTE session\n";
            tradeSession_.stop();
            tradeConnected_.store(false);
            state_.store(CTraderState::DISCONNECTED);
            return false;
        }
        
        fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] quoteSession_.start() OK, waiting for logon...\n");
        fflush(stderr);
        
        // Wait for QUOTE logon (60 seconds max, interruptible)
        // Increased from 30s because cTrader can be slow
        std::cout << "[CTraderFIX] Waiting for QUOTE logon...\n";
        for (int i = 0; i < 600 && !quoteConnected_.load() && !shouldStop(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (i > 0 && i % 50 == 0) {
                fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Still waiting for QUOTE logon... (%ds)\n", i/10);
                fflush(stderr);
                std::cout << "[CTraderFIX] Still waiting for QUOTE logon... (" << (i/10) << "s)\n";
            }
        }
        
        if (shouldStop()) {
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] Shutdown during QUOTE logon\n");
            fflush(stderr);
            std::cout << "[CTraderFIX] Shutdown requested during QUOTE logon\n";
            quoteSession_.stop();
            tradeSession_.stop();
            tradeConnected_.store(false);
            state_.store(CTraderState::DISCONNECTED);
            return false;
        }
        
        // Final check with small delay to handle race condition
        // where logon arrives at exact moment timeout expires
        if (!quoteConnected_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!quoteConnected_.load()) {
                fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] QUOTE logon TIMEOUT (after 60s)\n");
                fflush(stderr);
                std::cerr << "[CTraderFIX] FATAL: QUOTE session logon timeout (60s)\n";
                quoteSession_.stop();
                tradeSession_.stop();
                tradeConnected_.store(false);
                state_.store(CTraderState::DISCONNECTED);
                return false;
            }
            fprintf(stderr, "[FIX-DBG][CTraderFIXClient::connect] QUOTE logon arrived during grace period\n");
            fflush(stderr);
        }
        
        std::cout << "[CTraderFIX] ============================================\n";
        std::cout << "[CTraderFIX] BOTH SESSIONS CONNECTED!\n";
        std::cout << "[CTraderFIX] TRADE: ACTIVE (port " << config_.tradePort << ")\n";
        std::cout << "[CTraderFIX] QUOTE: ACTIVE (port " << config_.pricePort << ")\n";
        std::cout << "[CTraderFIX] State: " << toString(state_.load()) << "\n";
        std::cout << "[CTraderFIX] ============================================\n";
        
        return true;
    }
    
    void disconnect() {
        std::cout << "[CTraderFIX] Disconnecting...\n";
        
        // Set shutdown flag to interrupt any blocking waits in connect()
        shutdown_.store(true);
        
        // Mark as disconnected first to prevent any new operations
        state_.store(CTraderState::DISCONNECTED);
        
        // Disconnect QUOTE first (subordinate)
        quoteSession_.stop();
        quoteConnected_.store(false);
        
        // Disconnect TRADE last (primary)
        tradeSession_.stop();
        tradeConnected_.store(false);
        
        // Reset shutdown flag for potential reconnect
        shutdown_.store(false);
        
        std::cout << "[CTraderFIX] Fully disconnected\n";
    }
    
    bool isConnected() const {
        return quoteConnected_.load() && tradeConnected_.load();
    }
    
    bool isQuoteConnected() const { return quoteConnected_.load(); }
    bool isTradeConnected() const { return tradeConnected_.load(); }
    
    CTraderState getState() const { return state_.load(); }
    
    // =========================================================================
    // SECURITY LIST
    // =========================================================================
    
    bool requestSecurityList() {
        if (!quoteConnected_.load()) {
            std::cerr << "[CTraderFIX] Cannot request security list: QUOTE not connected\n";
            return false;
        }
        
        securityListReady_.store(false);
        securityListTotal_.store(0);
        
        std::cout << "[CTraderFIX] Requesting security list...\n";
        return quoteSession_.sendSecurityListRequest();
    }
    
    bool isSecurityListReady() const { return securityListReady_.load(); }
    int getSecurityListCount() const { return securityListTotal_.load(); }
    
    void setOnSecurityListReady(std::function<void()> cb) {
        onSecurityListReady_ = std::move(cb);
    }
    
    // Get SecurityID for a symbol (returns 0 if not found)
    int getSecurityId(const std::string& symbol) {
        std::string normalized = normalizeSymbol(symbol);
        std::lock_guard<std::mutex> lock(securityMtx_);
        auto it = symbolToId_.find(normalized);
        return (it != symbolToId_.end()) ? it->second : 0;
    }
    
    // Get symbol name for a SecurityID (returns empty if not found)
    std::string getSymbolName(int securityId) {
        std::lock_guard<std::mutex> lock(securityMtx_);
        auto it = idToSymbol_.find(securityId);
        return (it != idToSymbol_.end()) ? it->second : "";
    }
    
    // =========================================================================
    // MARKET DATA
    // =========================================================================
    
    bool subscribeMarketData(const std::string& symbol) {
        if (!quoteConnected_.load()) {
            std::cerr << "[CTraderFIX] Cannot subscribe: QUOTE not connected\n";
            return false;
        }
        
        // Normalize symbol for lookup (verify it exists in security list)
        std::string normalized = normalizeSymbol(symbol);
        int securityId = 0;
        {
            std::lock_guard<std::mutex> lock(securityMtx_);
            auto it = symbolToId_.find(normalized);
            if (it != symbolToId_.end()) {
                securityId = it->second;
            }
        }
        
        if (securityId == 0) {
            std::cerr << "[CTraderFIX] Symbol not in security list: " << symbol 
                      << " (normalized: " << normalized << ")\n";
            return false;
        }
        
        // =================================================================
        // cTrader QUOTE MarketDataRequest requires NUMERIC SecurityID in tag 55
        // Error: "Expected numeric symbolId, but got XAGUSD"
        // We must pass the numeric ID as a string, NOT the symbol name
        // =================================================================
        std::string securityIdStr = std::to_string(securityId);
        std::cout << "[CTraderFIX] Subscribing to " << normalized 
                  << " (SecurityID=" << securityIdStr << ")\n";
        return quoteSession_.sendMarketDataRequest(securityIdStr, true);
    }
    
    bool unsubscribeMarketData(const std::string& symbol) {
        if (!quoteConnected_.load()) return false;
        
        // Also need to use SecurityID for unsubscribe
        std::string normalized = normalizeSymbol(symbol);
        int securityId = 0;
        {
            std::lock_guard<std::mutex> lock(securityMtx_);
            auto it = symbolToId_.find(normalized);
            if (it != symbolToId_.end()) {
                securityId = it->second;
            }
        }
        if (securityId == 0) return false;
        
        return quoteSession_.sendMarketDataRequest(std::to_string(securityId), false);
    }
    
    // =========================================================================
    // ORDER ENTRY
    // v6.88 FIX: Added positionEffect - REQUIRED for cTrader CFDs
    // =========================================================================
    
    bool sendMarketOrder(const std::string& symbol, char side, double qty,
                         char positionEffect = FIXPositionEffect::Open) {
        if (!tradeConnected_.load()) {
            std::cerr << "[CTraderFIX] Cannot send order: TRADE not connected\n";
            return false;
        }
        
        std::cout << "[CTraderFIX] Sending MARKET order: " << symbol 
                  << " " << (side == FIXSide::Buy ? "BUY" : "SELL") 
                  << " " << qty 
                  << " posEffect=" << positionEffect << "\n";
        
        return tradeSession_.sendNewOrder(symbol, side, qty, FIXOrdType::Market, 0.0, positionEffect);
    }
    
    bool sendLimitOrder(const std::string& symbol, char side, double qty, double price,
                        char positionEffect = FIXPositionEffect::Open) {
        if (!tradeConnected_.load()) {
            std::cerr << "[CTraderFIX] Cannot send order: TRADE not connected\n";
            return false;
        }
        
        std::cout << "[CTraderFIX] Sending LIMIT order: " << symbol 
                  << " " << (side == FIXSide::Buy ? "BUY" : "SELL") 
                  << " " << qty << " @ " << price 
                  << " posEffect=" << positionEffect << "\n";
        
        return tradeSession_.sendNewOrder(symbol, side, qty, FIXOrdType::Limit, price, positionEffect);
    }
    
    // =========================================================================
    // STATISTICS
    // =========================================================================
    
    uint64_t getTickCount() const { return tickCount_.load(); }
    
    uint64_t getLatencyUs() const {
        if (tickCount_.load() == 0) return 0;
        // Latency only measured after first tick
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return now - firstTickTime_.load();
    }
    
    void printStats() const {
        const auto& qt = quoteSession_.getTransport();
        const auto& tt = tradeSession_.getTransport();
        
        std::cout << "\n=== CTrader FIX Statistics ===\n";
        std::cout << "State: " << toString(state_.load()) << "\n";
        std::cout << "TRADE: " << (tradeConnected_.load() ? "UP" : "DOWN") 
                  << " Sent=" << tt.getBytesSent() << "B, Recv=" << tt.getBytesRecv() 
                  << "B, Msgs=" << tt.getMsgsSent() << "/" << tt.getMsgsRecv() << "\n";
        std::cout << "QUOTE: " << (quoteConnected_.load() ? "UP" : "DOWN")
                  << " Sent=" << qt.getBytesSent() << "B, Recv=" << qt.getBytesRecv() 
                  << "B, Msgs=" << qt.getMsgsSent() << "/" << qt.getMsgsRecv() << "\n";
        std::cout << "Ticks: " << tickCount_.load() << "\n";
        std::cout << "==============================\n";
    }
    
private:
    // =========================================================================
    // MESSAGE HANDLERS
    // =========================================================================
    
    void handleQuoteMessage(const FIXMessage& msg) {
        char msgType = msg.getMsgType();
        
        switch (msgType) {
            case FIXMsgType::MarketDataSnapshot:
            case FIXMsgType::MarketDataIncremental:
                handleMarketData(msg);
                break;
                
            case FIXMsgType::MarketDataReject:
                handleMarketDataReject(msg);
                break;
                
            case 'y':  // SecurityList
                handleSecurityList(msg);
                break;
                
            default:
                break;
        }
    }
    
    void handleTradeMessage(const FIXMessage& msg) {
        char msgType = msg.getMsgType();
        
        switch (msgType) {
            case FIXMsgType::ExecutionReport:
                handleExecutionReport(msg);
                break;
                
            case FIXMsgType::OrderCancelReject:
                handleOrderCancelReject(msg);
                break;
                
            default:
                break;
        }
    }
    
    void handleMarketData(const FIXMessage& msg) {
        CTraderTick tick;
        tick.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // =================================================================
        // SYMBOL RESOLUTION
        // cTrader sends tag 55 as numeric SecurityID in market data
        // We need to look up the symbol name from our security list map
        // =================================================================
        std::string tag55Value = msg.getString(FIXTag::Symbol);
        
        // Try to parse as SecurityID (numeric)
        int securityId = 0;
        try {
            securityId = std::stoi(tag55Value);
        } catch (...) {
            // Not numeric - use as-is (might already be symbol name)
            tick.symbol = tag55Value;
        }
        
        // Look up symbol name from SecurityID
        if (securityId > 0) {
            std::lock_guard<std::mutex> lock(securityMtx_);
            auto it = idToSymbol_.find(securityId);
            if (it != idToSymbol_.end()) {
                tick.symbol = it->second;
            } else {
                // SecurityID not found - keep numeric value for debugging
                tick.symbol = tag55Value;
            }
        }
        
        // Record first tick time for latency calculation
        uint64_t count = tickCount_.fetch_add(1);
        if (count == 0) {
            firstTickTime_.store(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        
        // Parse MD entries
        // Format: 268=N (NoMDEntries) followed by repeating groups
        // Each entry: 269=type (0=Bid, 1=Offer), 270=price, 271=size
        
        tick.bid = 0.0;
        tick.ask = 0.0;
        tick.bidSize = 0.0;
        tick.askSize = 0.0;
        
        // Parse entries
        const char* buf = msg.buffer();
        uint32_t len = msg.bufferLen();
        
        int currentType = -1;
        for (uint32_t i = 0; i < len - 5; ++i) {
            // Check for 269= (MDEntryType)
            if (buf[i] == '2' && buf[i+1] == '6' && buf[i+2] == '9' && buf[i+3] == '=') {
                currentType = buf[i+4] - '0';
            }
            // Check for 270= (MDEntryPx)
            else if (buf[i] == '2' && buf[i+1] == '7' && buf[i+2] == '0' && buf[i+3] == '=') {
                uint32_t start = i + 4;
                uint32_t end = start;
                while (end < len && buf[end] != '\x01') end++;
                double px = fast_parse_double(buf + start, end - start);
                if (currentType == 0) tick.bid = px;
                else if (currentType == 1) tick.ask = px;
            }
            // Check for 271= (MDEntrySize)
            else if (buf[i] == '2' && buf[i+1] == '7' && buf[i+2] == '1' && buf[i+3] == '=') {
                uint32_t start = i + 4;
                uint32_t end = start;
                while (end < len && buf[end] != '\x01') end++;
                double sz = fast_parse_double(buf + start, end - start);
                if (currentType == 0) tick.bidSize = sz;
                else if (currentType == 1) tick.askSize = sz;
            }
        }
        
        if (tick.bid > 0 && tick.ask > 0 && onTick_) {
            onTick_(tick);
        }
    }
    
    void handleMarketDataReject(const FIXMessage& msg) {
        std::string mdReqID = msg.getString(FIXTag::MDReqID);
        std::string text = msg.getString(FIXTag::Text);
        std::cerr << "[CTraderFIX] MarketDataReject: " << mdReqID << " - " << text << "\n";
    }
    
    void handleSecurityList(const FIXMessage& msg) {
        // cTrader SecurityList format discovered from actual data:
        // 55=<numericId>|1007=<symbolName>|1008=<type>|55=<nextId>|1007=<nextSymbol>|...
        // Tag 55 = numeric security ID (e.g., 1, 2, 41)
        // Tag 1007 = symbol name (e.g., EURUSD, XAUUSD)
        // Tag 1008 = security type
        
        // Get raw message to parse repeating group
        const char* raw = msg.buffer();
        size_t rawLen = msg.bufferLen();
        if (!raw || rawLen == 0) {
            std::cerr << "[CTraderFIX] SecurityList: no raw data\n";
            return;
        }
        
        // Parse all 55 (ID) and 1007 (Symbol) pairs
        int currentId = 0;
        std::string currentSymbol;
        int entriesThisMsg = 0;
        
        size_t pos = 0;
        while (pos < rawLen) {
            // Find next '='
            size_t eq = rawLen;
            for (size_t i = pos; i < rawLen; i++) {
                if (raw[i] == '=') { eq = i; break; }
            }
            if (eq >= rawLen) break;
            
            // Find SOH
            size_t soh = rawLen;
            for (size_t i = eq + 1; i < rawLen; i++) {
                if (raw[i] == '\x01') { soh = i; break; }
            }
            
            // Parse tag number
            int tag = 0;
            for (size_t i = pos; i < eq; i++) {
                if (raw[i] >= '0' && raw[i] <= '9') {
                    tag = tag * 10 + (raw[i] - '0');
                }
            }
            
            // Extract value
            std::string value(raw + eq + 1, soh - eq - 1);
            
            // Process tags - cTrader format: 55=<id>|1007=<symbol>
            if (tag == 55) {  // Numeric ID
                // Save previous entry if complete (ID + Symbol pair)
                if (currentId != 0 && !currentSymbol.empty()) {
                    std::string normalized = normalizeSymbol(currentSymbol);
                    if (!normalized.empty()) {
                        std::lock_guard<std::mutex> lock(securityMtx_);
                        symbolToId_[normalized] = currentId;
                        idToSymbol_[currentId] = normalized;
                        entriesThisMsg++;
                    }
                }
                // Start new entry with ID
                currentId = 0;
                currentSymbol.clear();
                try { currentId = std::stoi(value); } catch (...) {}
            }
            else if (tag == 1007) {  // Symbol name follows the ID
                currentSymbol = value;
            }
            
            pos = soh + 1;
        }
        
        // Save last entry
        if (currentId != 0 && !currentSymbol.empty()) {
            std::string normalized = normalizeSymbol(currentSymbol);
            if (!normalized.empty()) {
                std::lock_guard<std::mutex> lock(securityMtx_);
                symbolToId_[normalized] = currentId;
                idToSymbol_[currentId] = normalized;
                entriesThisMsg++;
            }
        }
        
        securityListTotal_.fetch_add(entriesThisMsg);
        
        std::cout << "[CTraderFIX] SecurityList: " << entriesThisMsg 
                  << " entries parsed (total: " << securityListTotal_.load() << ")\n";
        
        // Mark ready and print key mappings
        if (entriesThisMsg > 0) {
            securityListReady_.store(true);
            
            std::lock_guard<std::mutex> lock(securityMtx_);
            
            // Print key mappings
            const char* important[] = {"EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "XAUUSD", "XAGUSD", "US30", "US100", "NAS100", "SPX500", "US500"};
            std::cout << "[CTraderFIX] Key symbol IDs:\n";
            for (const char* sym : important) {
                auto it = symbolToId_.find(sym);
                if (it != symbolToId_.end()) {
                    std::cout << "  " << sym << " = " << it->second << "\n";
                }
            }
            
            // Notify callback
            if (onSecurityListReady_) {
                onSecurityListReady_();
            }
        }
    }
    
    void handleExecutionReport(const FIXMessage& msg) {
        CTraderExecReport report;
        report.symbol = msg.getString(FIXTag::Symbol);
        report.clOrdID = msg.getString(FIXTag::ClOrdID);
        report.orderID = msg.getString(FIXTag::OrderID);
        report.execID = msg.getString(FIXTag::ExecID);
        
        FIXFieldView v;
        if (msg.getView(FIXTag::ExecType, v) && v.len > 0) {
            report.execType = v.ptr[0];
        }
        if (msg.getView(FIXTag::OrdStatus, v) && v.len > 0) {
            report.ordStatus = v.ptr[0];
        }
        if (msg.getView(FIXTag::Side, v) && v.len > 0) {
            report.side = v.ptr[0];
        }
        
        report.orderQty = msg.getDoubleFast(FIXTag::OrderQty);
        report.cumQty = msg.getDoubleFast(FIXTag::CumQty);
        report.leavesQty = msg.getDoubleFast(FIXTag::LeavesQty);
        report.avgPx = msg.getDoubleFast(FIXTag::AvgPx);
        report.lastPx = msg.getDoubleFast(FIXTag::LastPx);
        report.lastQty = msg.getDoubleFast(FIXTag::LastQty);
        report.text = msg.getString(FIXTag::Text);
        
        report.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::cout << "[CTraderFIX] ExecReport: " << report.symbol 
                  << " ExecType=" << report.execType 
                  << " Status=" << report.ordStatus
                  << " CumQty=" << report.cumQty
                  << " AvgPx=" << report.avgPx << "\n";
        
        if (onExec_) {
            onExec_(report);
        }
    }
    
    void handleOrderCancelReject(const FIXMessage& msg) {
        std::string clOrdID = msg.getString(FIXTag::ClOrdID);
        std::string text = msg.getString(FIXTag::Text);
        std::cerr << "[CTraderFIX] OrderCancelReject: " << clOrdID << " - " << text << "\n";
    }
    
    void notifyState() {
        if (onState_) {
            onState_(quoteConnected_.load(), tradeConnected_.load());
        }
    }
    
    // Normalize symbol name (uppercase, strip suffixes)
    std::string normalizeSymbol(const std::string& sym) {
        std::string result;
        result.reserve(sym.size());
        for (char c : sym) {
            if (c >= 'a' && c <= 'z') {
                result += (c - 'a' + 'A');
            } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                result += c;
            }
        }
        // Strip common suffixes
        if (result.size() > 3) {
            if (result.substr(result.size()-3) == ".FX" || 
                result.substr(result.size()-3) == "FX.") {
                result = result.substr(0, result.size()-3);
            }
        }
        if (result.size() > 5) {
            if (result.substr(result.size()-5) == ".CASH") {
                result = result.substr(0, result.size()-5);
            }
        }
        return result;
    }
    
private:
    FIXConfig config_;
    
    FIXSession quoteSession_;
    FIXSession tradeSession_;
    
    std::atomic<bool> quoteConnected_;
    std::atomic<bool> tradeConnected_;
    std::atomic<bool> shutdown_;  // Flag to interrupt connect() during shutdown
    std::atomic<bool>* externalRunning_;  // Pointer to CfdEngine's running_ flag
    std::atomic<CTraderState> state_;
    
    std::atomic<uint64_t> tickCount_;
    std::atomic<uint64_t> firstTickTime_;
    
    // Security List mapping
    std::mutex securityMtx_;
    std::map<std::string, int> symbolToId_;      // Symbol -> SecurityID
    std::map<int, std::string> idToSymbol_;      // SecurityID -> Symbol
    std::atomic<bool> securityListReady_{false};
    std::atomic<int> securityListTotal_{0};
    std::function<void()> onSecurityListReady_;
    
    CTraderTickCallback onTick_;
    CTraderExecCallback onExec_;
    CTraderStateCallback onState_;
};

} // namespace Chimera
