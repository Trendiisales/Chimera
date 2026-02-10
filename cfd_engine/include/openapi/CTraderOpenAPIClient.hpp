#pragma once
// =============================================================================
// CTRADER OPEN API CLIENT - BINARY ENVELOPE FORMAT
// v4.14.11 - clientMsgId in BINARY ENVELOPE not protobuf wrapper
// =============================================================================

#include "../CTraderTypes.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <array>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <random>

// OpenSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

// Networking
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace Chimera {

// =============================================================================
// OPEN API PAYLOAD TYPES (from Spotware proto files)
// =============================================================================
namespace ProtoOAPayloadType {
    constexpr uint32_t PROTO_OA_APPLICATION_AUTH_REQ = 2100;
    constexpr uint32_t PROTO_OA_APPLICATION_AUTH_RES = 2101;
    constexpr uint32_t PROTO_OA_ACCOUNT_AUTH_REQ = 2102;
    constexpr uint32_t PROTO_OA_ACCOUNT_AUTH_RES = 2103;
    constexpr uint32_t PROTO_OA_VERSION_REQ = 2104;
    constexpr uint32_t PROTO_OA_VERSION_RES = 2105;
    constexpr uint32_t PROTO_OA_NEW_ORDER_REQ = 2106;
    constexpr uint32_t PROTO_OA_CANCEL_ORDER_REQ = 2108;
    constexpr uint32_t PROTO_OA_CLOSE_POSITION_REQ = 2111;
    constexpr uint32_t PROTO_OA_SYMBOLS_LIST_REQ = 2114;
    constexpr uint32_t PROTO_OA_SYMBOLS_LIST_RES = 2115;
    constexpr uint32_t PROTO_OA_SYMBOL_BY_ID_REQ = 2116;
    constexpr uint32_t PROTO_OA_SYMBOL_BY_ID_RES = 2117;
    constexpr uint32_t PROTO_OA_SUBSCRIBE_SPOTS_REQ = 2124;
    constexpr uint32_t PROTO_OA_SUBSCRIBE_SPOTS_RES = 2125;
    constexpr uint32_t PROTO_OA_SPOT_EVENT = 2126;           // TICK DATA - THIS IS THE KEY ONE!
    constexpr uint32_t PROTO_OA_UNSUBSCRIBE_SPOTS_REQ = 2127;
    constexpr uint32_t PROTO_OA_UNSUBSCRIBE_SPOTS_RES = 2128;
    constexpr uint32_t PROTO_OA_EXECUTION_EVENT = 2124;      // Actually different, but not using it
    constexpr uint32_t PROTO_OA_ORDER_ERROR_EVENT = 2132;
    constexpr uint32_t PROTO_OA_GET_ACCOUNTS_BY_ACCESS_TOKEN_REQ = 2149;
    constexpr uint32_t PROTO_OA_GET_ACCOUNTS_BY_ACCESS_TOKEN_RES = 2150;
    constexpr uint32_t PROTO_OA_ERROR_RES = 2142;
    
    // Common messages
    constexpr uint32_t PROTO_HEARTBEAT_EVENT = 51;
    constexpr uint32_t ERROR_RES = 50;
}

// =============================================================================
// PROTOBUF WIRE FORMAT ENCODER
// =============================================================================
class ProtobufEncoder {
public:
    std::vector<uint8_t> data;
    
    void clear() { data.clear(); }
    size_t size() const { return data.size(); }
    
    void writeVarint(uint64_t value) {
        while (value > 0x7F) {
            data.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        data.push_back(static_cast<uint8_t>(value));
    }
    
    void writeTag(uint32_t fieldNumber, uint32_t wireType) {
        writeVarint((fieldNumber << 3) | wireType);
    }
    
    void writeUint32(uint32_t fieldNumber, uint32_t value) {
        writeTag(fieldNumber, 0);
        writeVarint(value);
    }
    
    void writeInt64(uint32_t fieldNumber, int64_t value) {
        writeTag(fieldNumber, 0);
        writeVarint(static_cast<uint64_t>(value));
    }
    
    void writeString(uint32_t fieldNumber, const std::string& value) {
        writeTag(fieldNumber, 2);
        writeVarint(value.size());
        data.insert(data.end(), value.begin(), value.end());
    }
    
    void writeBytes(uint32_t fieldNumber, const std::vector<uint8_t>& value) {
        writeTag(fieldNumber, 2);
        writeVarint(value.size());
        data.insert(data.end(), value.begin(), value.end());
    }
    
    std::vector<uint8_t> finish() { return data; }
};

// =============================================================================
// PROTOBUF WIRE FORMAT DECODER
// =============================================================================
class ProtobufDecoder {
public:
    const uint8_t* ptr;
    const uint8_t* end;
    
    ProtobufDecoder(const uint8_t* data, size_t len) 
        : ptr(data), end(data + len) {}
    
    bool hasMore() const { return ptr < end; }
    
    uint64_t readVarint() {
        uint64_t result = 0;
        int shift = 0;
        while (ptr < end) {
            uint8_t b = *ptr++;
            result |= static_cast<uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        return result;
    }
    
    std::pair<uint32_t, uint32_t> readTag() {
        uint64_t tag = readVarint();
        return {static_cast<uint32_t>(tag >> 3), static_cast<uint32_t>(tag & 0x7)};
    }
    
    std::string readString() {
        size_t len = readVarint();
        if (ptr + len > end) len = end - ptr;
        std::string result(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
        return result;
    }
    
    std::vector<uint8_t> readBytes() {
        size_t len = readVarint();
        if (ptr + len > end) len = end - ptr;
        std::vector<uint8_t> result(ptr, ptr + len);
        ptr += len;
        return result;
    }
    
    void skipField(uint32_t wireType) {
        switch (wireType) {
            case 0: readVarint(); break;
            case 1: ptr += 8; break;
            case 2: { size_t len = readVarint(); ptr += len; } break;
            case 5: ptr += 4; break;
        }
    }
};

// =============================================================================
// DEBUG HELPER - Full hex dump with ASCII
// =============================================================================
inline void hexDump(const char* label, const uint8_t* data, size_t len) {
    std::cout << "[OpenAPI] " << label << " (" << len << " bytes):\n";
    for (size_t i = 0; i < len; i += 16) {
        printf("  %04zx: ", i);
        for (size_t j = 0; j < 16 && i + j < len; ++j) {
            printf("%02X ", data[i + j]);
        }
        for (size_t j = len - i; j < 16 && i + j >= len; ++j) {
            printf("   ");
        }
        printf(" | ");
        for (size_t j = 0; j < 16 && i + j < len; ++j) {
            char c = static_cast<char>(data[i + j]);
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}

// =============================================================================
// CTRADER OPEN API CLIENT
// =============================================================================
class CTraderOpenAPIClient {
public:
    CTraderOpenAPIClient() = default;
    ~CTraderOpenAPIClient() { disconnect(); }
    
    void setConfig(const OpenAPIConfig& cfg) {
        config_ = cfg;
        std::cout << "[OpenAPI] Config set for " << (cfg.isLive ? "LIVE" : "DEMO") 
                  << " account " << cfg.accountId << "\n";
        std::cout << "[OpenAPI] Host: " << cfg.host << ":" << cfg.port << "\n";
        std::cout << "[OpenAPI] ClientID: " << cfg.clientId << "\n";
        std::cout << "[OpenAPI] ClientSecret length: " << cfg.clientSecret.length() << "\n";
        std::cout << "[OpenAPI] AccessToken length: " << cfg.accessToken.length() << "\n";
    }
    
    void setOnTick(CTraderTickCallback cb) { tickCallback_ = std::move(cb); }
    void setOnExec(CTraderExecCallback cb) { execCallback_ = std::move(cb); }
    void setOnState(CTraderStateCallback cb) { stateCallback_ = std::move(cb); }
    
    // =================================================================
    // LATENCY STATS - True OpenAPI round-trip measurements
    // =================================================================
    struct LatencyReport {
        double heartbeat_rtt_us;      // Latest heartbeat RTT
        double heartbeat_avg_us;
        double heartbeat_min_us;
        double heartbeat_max_us;
        uint64_t heartbeat_count;
        
        double order_rtt_us;          // Latest order RTT (send→ACK)
        double order_avg_us;
        double order_min_us;
        double order_max_us;
        uint64_t order_count;
        
        double tick_latency_us;       // Server timestamp delta
    };
    
    LatencyReport getLatencyStats() const {
        LatencyReport r;
        r.heartbeat_rtt_us = static_cast<double>(latency_.heartbeat_rtt_us.load());
        r.heartbeat_avg_us = latency_.heartbeatAvgUs();
        r.heartbeat_min_us = latency_.heartbeat_min_us.load() == UINT64_MAX ? 0.0 : 
                             static_cast<double>(latency_.heartbeat_min_us.load());
        r.heartbeat_max_us = static_cast<double>(latency_.heartbeat_max_us.load());
        r.heartbeat_count = latency_.heartbeat_count.load();
        
        r.order_rtt_us = static_cast<double>(latency_.order_rtt_us.load());
        r.order_avg_us = latency_.orderAvgUs();
        r.order_min_us = latency_.order_min_us.load() == UINT64_MAX ? 0.0 :
                         static_cast<double>(latency_.order_min_us.load());
        r.order_max_us = static_cast<double>(latency_.order_max_us.load());
        r.order_count = latency_.order_count.load();
        
        r.tick_latency_us = static_cast<double>(latency_.tick_latency_us.load());
        return r;
    }
    
    void printLatencyStats() const {
        auto r = getLatencyStats();
        std::cout << "\n[LATENCY] OpenAPI Round-Trip Stats:\n"
                  << "  Heartbeat: " << std::fixed << std::setprecision(0) 
                  << r.heartbeat_rtt_us << "us (avg=" << r.heartbeat_avg_us 
                  << " min=" << r.heartbeat_min_us << " max=" << r.heartbeat_max_us 
                  << " n=" << r.heartbeat_count << ")\n"
                  << "  Order RTT: " << r.order_rtt_us << "us (avg=" << r.order_avg_us
                  << " min=" << r.order_min_us << " max=" << r.order_max_us
                  << " n=" << r.order_count << ")\n";
    }
    
    bool connect() {
        std::cout << "[OpenAPI] ========================================\n";
        std::cout << "[OpenAPI] CONNECT START v4.14.11 (BINARY ENVELOPE)\n";
        std::cout << "[OpenAPI] ========================================\n";
        
        // Safety: ensure any previous threads are cleaned up
        running_.store(false);
        if (heartbeatThread_.joinable()) {
            heartbeatThread_.join();
        }
        if (recvThread_.joinable()) {
            recvThread_.join();
        }
        
        // Reset auth state
        appAuthed_.store(false);
        accountAuthed_.store(false);
        symbolsLoaded_.store(false);
        
        std::cout << "[OpenAPI] Connecting to " << config_.host << ":" << config_.port << "...\n";
        
        // Validate config
        if (config_.clientId.empty()) {
            std::cerr << "[OpenAPI] ERROR: clientId is empty!\n";
            return false;
        }
        if (config_.clientSecret.empty()) {
            std::cerr << "[OpenAPI] ERROR: clientSecret is empty!\n";
            return false;
        }
        if (config_.accessToken.empty()) {
            std::cerr << "[OpenAPI] ERROR: accessToken is empty!\n";
            return false;
        }
        if (config_.accountId == 0) {
            std::cerr << "[OpenAPI] ERROR: accountId is 0!\n";
            return false;
        }
        
        // Create socket
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "[OpenAPI] Socket creation failed: " << strerror(errno) << "\n";
            return false;
        }
        std::cout << "[OpenAPI] Socket created: fd=" << sockfd_ << "\n";
        
        // Resolve host
        struct hostent* server = gethostbyname(config_.host.c_str());
        if (!server) {
            std::cerr << "[OpenAPI] Host resolution failed for " << config_.host << "\n";
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        std::cout << "[OpenAPI] Host resolved\n";
        
        // Connect
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config_.port);
        std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
        
        if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[OpenAPI] TCP connect failed: " << strerror(errno) << "\n";
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        std::cout << "[OpenAPI] TCP connected\n";
        
        // Initialize SSL
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        
        sslCtx_ = SSL_CTX_new(TLS_client_method());
        if (!sslCtx_) {
            std::cerr << "[OpenAPI] SSL context creation failed\n";
            ERR_print_errors_fp(stderr);
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        
        ssl_ = SSL_new(sslCtx_);
        SSL_set_fd(ssl_, sockfd_);
        
        int ssl_ret = SSL_connect(ssl_);
        if (ssl_ret <= 0) {
            int ssl_err = SSL_get_error(ssl_, ssl_ret);
            std::cerr << "[OpenAPI] SSL handshake failed, error=" << ssl_err << "\n";
            ERR_print_errors_fp(stderr);
            SSL_free(ssl_);
            ssl_ = nullptr;
            SSL_CTX_free(sslCtx_);
            sslCtx_ = nullptr;
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        
        std::cout << "[OpenAPI] SSL connected, cipher: " << SSL_get_cipher(ssl_) << "\n";
        
        connected_.store(true);
        running_.store(true);
        
        // Start receive thread
        recvThread_ = std::thread(&CTraderOpenAPIClient::receiveLoop, this);
        
        // Authenticate application
        std::cout << "[OpenAPI] ========================================\n";
        std::cout << "[OpenAPI] APP AUTH\n";
        std::cout << "[OpenAPI] ========================================\n";
        if (!authenticateApplication()) {
            std::cerr << "[OpenAPI] Application authentication failed\n";
            disconnect();
            return false;
        }
        
        // Authenticate account
        std::cout << "[OpenAPI] ========================================\n";
        std::cout << "[OpenAPI] ACCOUNT AUTH\n";
        std::cout << "[OpenAPI] ========================================\n";
        if (!authenticateAccount()) {
            std::cerr << "[OpenAPI] Account authentication failed\n";
            disconnect();
            return false;
        }
        
        // Request symbols list
        std::cout << "[OpenAPI] ========================================\n";
        std::cout << "[OpenAPI] SYMBOLS LIST\n";
        std::cout << "[OpenAPI] ========================================\n";
        requestSymbolsList();
        
        std::cout << "[OpenAPI] ========================================\n";
        std::cout << "[OpenAPI] CONNECT SUCCESS\n";
        std::cout << "[OpenAPI] ========================================\n";
        
        // Start heartbeat probe thread for latency measurement
        heartbeatThread_ = std::thread([this]() { heartbeatProbeLoop(); });
        
        if (stateCallback_) {
            stateCallback_(true);
        }
        
        return true;
    }
    
    void disconnect() {
        std::cout << "[OpenAPI] Disconnecting...\n";
        running_.store(false);
        connected_.store(false);
        
        if (heartbeatThread_.joinable()) {
            heartbeatThread_.join();
        }
        if (recvThread_.joinable()) {
            recvThread_.join();
        }
        
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (sslCtx_) {
            SSL_CTX_free(sslCtx_);
            sslCtx_ = nullptr;
        }
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
        
        if (stateCallback_) {
            stateCallback_(false);
        }
        
        std::cout << "[OpenAPI] Disconnected\n";
    }
    
    bool isConnected() const { 
        bool conn = connected_.load();
        bool app = appAuthed_.load();
        bool acct = accountAuthed_.load();
        
        // Debug output if not fully connected
        static bool lastState = false;
        bool currentState = conn && app && acct;
        if (currentState != lastState) {
            std::cout << "[OpenAPI] Connection state: connected=" << conn 
                      << " appAuth=" << app << " accountAuth=" << acct 
                      << " -> " << (currentState ? "READY" : "NOT READY") << "\n";
            lastState = currentState;
        }
        
        return currentState;
    }
    
    bool isSecurityListReady() const {
        return symbolsLoaded_.load();
    }
    
    bool subscribeMarketData(const std::string& symbol) {
        // Single symbol subscription - delegates to batch
        return subscribeMarketDataBatch({symbol});
    }
    
    bool subscribeMarketDataBatch(const std::vector<std::string>& symbols) {
        if (!isConnected()) {
            std::cerr << "[OpenAPI] Cannot subscribe - not connected\n";
            return false;
        }
        
        if (!appAuthed_.load() || !accountAuthed_.load()) {
            std::cerr << "[OpenAPI] ERROR: Not fully authenticated!\n";
            return false;
        }
        
        // Collect all symbol IDs
        std::vector<int64_t> symbolIds;
        for (const auto& symbol : symbols) {
            int64_t symbolId = getSymbolId(symbol);
            if (symbolId == 0) {
                std::cerr << "[OpenAPI] Unknown symbol: " << symbol << "\n";
                continue;
            }
            symbolIds.push_back(symbolId);
            std::cout << "[OpenAPI] SUBSCRIBE: " << symbol << " (ID=" << symbolId << ")\n";
        }
        
        if (symbolIds.empty()) {
            std::cerr << "[OpenAPI] No valid symbols to subscribe\n";
            return false;
        }
        
        // ProtoOASubscribeSpotsReq:
        //   field 2: ctidTraderAccountId (required)
        //   field 3: symbolId (repeated) - ALL symbols in ONE message
        ProtobufEncoder inner;
        inner.writeInt64(2, static_cast<int64_t>(config_.accountId));
        
        // Write EACH symbolId with field 3 tag (repeated field)
        for (int64_t sid : symbolIds) {
            inner.writeInt64(3, sid);
        }
        
        auto innerData = inner.finish();
        
        std::cout << "[OpenAPI] BATCH SUBSCRIPTION (" << symbolIds.size() << " symbols)\n";
        std::cout << "[OpenAPI]   AccountId: " << config_.accountId << "\n";
        std::cout << "[OpenAPI]   SymbolIds: ";
        for (auto id : symbolIds) std::cout << id << " ";
        std::cout << "\n";
        std::cout << "[OpenAPI]   Payload (" << innerData.size() << " bytes): ";
        for (auto b : innerData) printf("%02X ", b);
        std::cout << "\n";
        
        return sendProtoMessage(ProtoOAPayloadType::PROTO_OA_SUBSCRIBE_SPOTS_REQ, innerData);
    }
    
    bool sendMarketOrder(const std::string& symbol, char side, double qty) {
        if (!isConnected()) return false;
        
        int64_t symbolId = getSymbolId(symbol);
        if (symbolId == 0) return false;
        
        int64_t volume = static_cast<int64_t>(qty * 100 * 100000);
        
        // ProtoOANewOrderReq:
        //   field 1: payloadType = OPTIONAL (DON'T SEND - already in wrapper)
        //   field 2: ctidTraderAccountId
        //   field 3: symbolId
        //   field 4: orderType (1=MARKET)
        //   field 5: tradeSide (1=BUY, 2=SELL)
        //   field 6: volume (in cents)
        ProtobufEncoder inner;
        inner.writeInt64(2, static_cast<int64_t>(config_.accountId));
        inner.writeInt64(3, symbolId);
        inner.writeInt64(4, 1);  // MARKET order
        inner.writeInt64(5, side == OrderSide::Buy ? 1 : 2);
        inner.writeInt64(6, volume);
        
        std::cout << "[OpenAPI] Sending " << (side == OrderSide::Buy ? "BUY" : "SELL") 
                  << " " << qty << " " << symbol << "\n";
        
        return sendProtoMessage(ProtoOAPayloadType::PROTO_OA_NEW_ORDER_REQ, inner.finish());
    }
    
    int64_t getSymbolId(const std::string& symbol) {
        // First try exact match
        auto it = symbolIdCache_.find(symbol);
        if (it != symbolIdCache_.end()) {
            return it->second;
        }
        
        // Try common aliases
        static const std::map<std::string, std::vector<std::string>> aliases = {
            {"XAUUSD", {"GOLD", "XAU/USD", "XAUUSD."}},
            {"NAS100", {"USTEC", "NAS100.", "NASDAQ100", "US100", "NDX100"}},
            {"US30",   {"DJ30", "US30.", "DOW30", "DOWJONES", "DJI30"}}
        };
        
        auto aliasIt = aliases.find(symbol);
        if (aliasIt != aliases.end()) {
            for (const auto& alias : aliasIt->second) {
                auto found = symbolIdCache_.find(alias);
                if (found != symbolIdCache_.end()) {
                    std::cout << "[OpenAPI] Symbol " << symbol << " mapped to " << alias << " (ID: " << found->second << ")\n";
                    // Cache the mapping for future lookups
                    symbolIdCache_[symbol] = found->second;
                    symbolNameCache_[found->second] = symbol;  // Map back to our standard name
                    return found->second;
                }
            }
        }
        
        // Symbol not found in cache - it wasn't in the broker's symbol list
        std::cerr << "[OpenAPI] WARNING: Symbol " << symbol << " not found in broker symbol list!\n";
        std::cerr << "[OpenAPI] Available symbols (" << symbolIdCache_.size() << " total):\n";
        
        // Print first 20 symbols to help debug
        int count = 0;
        for (const auto& [name, id] : symbolIdCache_) {
            if (count++ < 20) {
                std::cerr << "[OpenAPI]   " << name << " = " << id << "\n";
            }
        }
        if (symbolIdCache_.size() > 20) {
            std::cerr << "[OpenAPI]   ... and " << (symbolIdCache_.size() - 20) << " more\n";
        }
        
        return 0;
    }
    
    // Wait for symbols to be loaded before subscribing
    bool waitForSymbols(int timeout_seconds = 10) {
        std::cout << "[OpenAPI] Waiting for symbols list...\n";
        for (int i = 0; i < timeout_seconds * 10; ++i) {
            if (symbolsLoaded_.load()) {
                std::cout << "[OpenAPI] Symbols loaded, " << symbolIdCache_.size() << " symbols available\n";
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cerr << "[OpenAPI] Timeout waiting for symbols list!\n";
        return false;
    }

private:
    OpenAPIConfig config_;
    int sockfd_ = -1;
    SSL_CTX* sslCtx_ = nullptr;
    SSL* ssl_ = nullptr;
    
    std::thread recvThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> appAuthed_{false};
    std::atomic<bool> accountAuthed_{false};
    std::atomic<bool> symbolsLoaded_{false};
    
    std::map<std::string, int64_t> symbolIdCache_;
    std::map<int64_t, std::string> symbolNameCache_;
    std::map<int64_t, int> symbolDigitsCache_;  // symbolId -> digits (e.g., 2 for XAUUSD, 1 for indices)
    
    CTraderTickCallback tickCallback_;
    CTraderExecCallback execCallback_;
    CTraderStateCallback stateCallback_;
    
    std::mutex sendMutex_;
    
    // =================================================================
    // LATENCY TRACKING - True round-trip measurement
    // =================================================================
    struct LatencyStats {
        std::atomic<uint64_t> heartbeat_rtt_us{0};      // Latest heartbeat RTT
        std::atomic<uint64_t> heartbeat_min_us{UINT64_MAX};
        std::atomic<uint64_t> heartbeat_max_us{0};
        std::atomic<uint64_t> heartbeat_sum_us{0};
        std::atomic<uint64_t> heartbeat_count{0};
        
        std::atomic<uint64_t> order_rtt_us{0};          // Latest order RTT
        std::atomic<uint64_t> order_min_us{UINT64_MAX};
        std::atomic<uint64_t> order_max_us{0};
        std::atomic<uint64_t> order_sum_us{0};
        std::atomic<uint64_t> order_count{0};
        
        std::atomic<uint64_t> tick_latency_us{0};       // Server timestamp vs local time
        
        double heartbeatAvgUs() const {
            auto c = heartbeat_count.load();
            return c > 0 ? static_cast<double>(heartbeat_sum_us.load()) / c : 0.0;
        }
        double orderAvgUs() const {
            auto c = order_count.load();
            return c > 0 ? static_cast<double>(order_sum_us.load()) / c : 0.0;
        }
    };
    
    LatencyStats latency_;
    std::thread heartbeatThread_;
    
    // Track pending requests by clientMsgId for latency measurement
    struct PendingRequest {
        uint64_t send_time_us;
        uint32_t msg_type;
    };
    std::map<std::string, PendingRequest> pendingRequests_;
    std::mutex pendingMutex_;
    
    // Random clientMsgId generator - MUST be truly random, not sequential!
    std::mt19937_64 rng_{std::random_device{}()};
    std::mutex rngMutex_;
    
    std::string nextClientMsgId() {
        std::lock_guard<std::mutex> lock(rngMutex_);
        uint64_t v = 0;
        while (v == 0) {
            v = rng_();
        }
        return std::to_string(v);
    }
    
    // =================================================================
    // STANDARD PROTOBUF-WRAPPED FORMAT (original approach)
    // ProtoMessage wrapper with clientMsgId as field 3
    // =================================================================
    bool sendProtoMessage(uint32_t payloadType, const std::vector<uint8_t>& payload) {
        std::string clientMsgId = nextClientMsgId();
        
        std::cout << "[OpenAPI] Building ProtoMessage (PROTOBUF WRAPPER):\n";
        std::cout << "[OpenAPI]   payloadType=" << payloadType << "\n";
        std::cout << "[OpenAPI]   payload size=" << payload.size() << " bytes\n";
        std::cout << "[OpenAPI]   clientMsgId=\"" << clientMsgId << "\"\n";
        
        // ProtoMessage wrapper:
        //   field 1: payloadType (varint)
        //   field 2: payload (bytes)
        //   field 3: clientMsgId (string)
        ProtobufEncoder msg;
        msg.writeUint32(1, payloadType);
        if (!payload.empty()) {
            msg.writeBytes(2, payload);
        }
        msg.writeString(3, clientMsgId);
        
        std::vector<uint8_t> msgData = msg.finish();
        
        // Frame: 4-byte BIG-ENDIAN length prefix
        uint32_t len = static_cast<uint32_t>(msgData.size());
        std::vector<uint8_t> frame(4 + msgData.size());
        frame[0] = (len >> 24) & 0xFF;
        frame[1] = (len >> 16) & 0xFF;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        std::memcpy(frame.data() + 4, msgData.data(), msgData.size());
        
        std::cout << "[OpenAPI] TX Frame (PROTOBUF wrapped, " << frame.size() << " bytes):\n";
        hexDump("TX", frame.data(), frame.size());
        
        std::lock_guard<std::mutex> lock(sendMutex_);
        int sent = SSL_write(ssl_, frame.data(), frame.size());
        if (sent <= 0) {
            int err = SSL_get_error(ssl_, sent);
            std::cerr << "[OpenAPI] SSL_write error: " << err << "\n";
            ERR_print_errors_fp(stderr);
            return false;
        }
        
        std::cout << "[OpenAPI] Sent " << sent << " bytes (protobuf format)\n";
        return true;
    }
    
    // =================================================================
    // ALTERNATIVE: BINARY ENVELOPE FORMAT
    // [4 bytes: length BE] [payloadType varint] [payload bytes] [clientMsgId varint]
    // This writes clientMsgId directly into ProtoMessage as it should be
    // =================================================================
    bool sendProtoMessageAlt(uint32_t payloadType, const std::vector<uint8_t>& payload) {
        std::string clientMsgId = nextClientMsgId();
        
        std::cout << "[OpenAPI] Building ProtoMessage (ALT FORMAT):\n";
        std::cout << "[OpenAPI]   payloadType=" << payloadType << "\n";
        std::cout << "[OpenAPI]   payload size=" << payload.size() << " bytes\n";
        std::cout << "[OpenAPI]   clientMsgId=\"" << clientMsgId << "\"\n";
        
        // Build the ProtoMessage fields manually with clientMsgId FIRST
        // Then payload. Different ordering might help.
        ProtobufEncoder msg;
        msg.writeUint32(1, payloadType);
        // Write clientMsgId as STRING (field 3) BEFORE payload
        msg.writeString(3, clientMsgId);
        if (!payload.empty()) {
            msg.writeBytes(2, payload);
        }
        
        std::vector<uint8_t> msgData = msg.finish();
        
        // Frame: 4-byte BIG-ENDIAN length prefix
        uint32_t len = static_cast<uint32_t>(msgData.size());
        std::vector<uint8_t> frame(4 + msgData.size());
        frame[0] = (len >> 24) & 0xFF;
        frame[1] = (len >> 16) & 0xFF;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        std::memcpy(frame.data() + 4, msgData.data(), msgData.size());
        
        std::cout << "[OpenAPI] TX Frame (ALT order, " << frame.size() << " bytes):\n";
        hexDump("TX-ALT", frame.data(), frame.size());
        
        std::lock_guard<std::mutex> lock(sendMutex_);
        int sent = SSL_write(ssl_, frame.data(), frame.size());
        if (sent <= 0) {
            int err = SSL_get_error(ssl_, sent);
            std::cerr << "[OpenAPI] SSL_write error: " << err << "\n";
            ERR_print_errors_fp(stderr);
            return false;
        }
        
        std::cout << "[OpenAPI] Sent " << sent << " bytes (alt format)\n";
        return true;
    }
    
    bool authenticateApplication() {
        std::cout << "[OpenAPI] Authenticating application:\n";
        std::cout << "[OpenAPI]   clientId: " << config_.clientId << "\n";
        std::cout << "[OpenAPI]   clientSecret: " << config_.clientSecret.substr(0, 10) << "... (len=" << config_.clientSecret.length() << ")\n";
        
        // ProtoOAApplicationAuthReq - cTrader reserves field 1 for payloadType
        //   field 2: clientId (string)
        //   field 3: clientSecret (string)
        ProtobufEncoder inner;
        inner.writeString(2, config_.clientId);
        inner.writeString(3, config_.clientSecret);
        
        std::cout << "[OpenAPI] ProtoOAApplicationAuthReq payload: " << inner.size() << " bytes\n";
        hexDump("AppAuthReq payload", inner.data.data(), inner.data.size());
        
        if (!sendProtoMessage(ProtoOAPayloadType::PROTO_OA_APPLICATION_AUTH_REQ, inner.finish())) {
            std::cerr << "[OpenAPI] Failed to send app auth request\n";
            return false;
        }
        
        std::cout << "[OpenAPI] Waiting for app auth response...\n";
        for (int i = 0; i < 100 && !appAuthed_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!connected_.load()) {
                std::cerr << "[OpenAPI] Connection lost during app auth\n";
                return false;
            }
        }
        
        if (!appAuthed_.load()) {
            std::cerr << "[OpenAPI] App auth timeout after 10 seconds\n";
        }
        
        return appAuthed_.load();
    }
    
    bool authenticateAccount() {
        std::cout << "[OpenAPI] Authenticating account:\n";
        std::cout << "[OpenAPI]   accountId: " << config_.accountId << "\n";
        std::cout << "[OpenAPI]   accessToken: " << config_.accessToken.substr(0, 20) << "...\n";
        
        // ProtoOAAccountAuthReq - cTrader reserves field 1 for payloadType
        //   field 2: ctidTraderAccountId (int64)
        //   field 3: accessToken (string)
        ProtobufEncoder inner;
        inner.writeInt64(2, static_cast<int64_t>(config_.accountId));
        inner.writeString(3, config_.accessToken);
        
        std::cout << "[OpenAPI] ProtoOAAccountAuthReq payload: " << inner.size() << " bytes\n";
        
        if (!sendProtoMessage(ProtoOAPayloadType::PROTO_OA_ACCOUNT_AUTH_REQ, inner.finish())) {
            std::cerr << "[OpenAPI] Failed to send account auth request\n";
            return false;
        }
        
        std::cout << "[OpenAPI] Waiting for account auth response...\n";
        for (int i = 0; i < 100 && !accountAuthed_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!connected_.load()) {
                std::cerr << "[OpenAPI] Connection lost during account auth\n";
                return false;
            }
        }
        
        if (!accountAuthed_.load()) {
            std::cerr << "[OpenAPI] Account auth timeout after 10 seconds\n";
        }
        
        return accountAuthed_.load();
    }
    
    void requestSymbolsList() {
        std::cout << "[OpenAPI] Requesting symbols list for account " << config_.accountId << "\n";
        
        // ProtoOASymbolsListReq - field 2 for accountId (confirmed working)
        ProtobufEncoder inner;
        inner.writeInt64(2, static_cast<int64_t>(config_.accountId));
        
        sendProtoMessage(ProtoOAPayloadType::PROTO_OA_SYMBOLS_LIST_REQ, inner.finish());
    }
    
    // =================================================================
    // HEARTBEAT PROBE LOOP - Measures true network RTT
    // =================================================================
    void heartbeatProbeLoop() {
        std::cout << "[LATENCY] Heartbeat probe loop started\n";
        
        while (running_.load() && connected_.load()) {
            // Wait 5 seconds between probes
            for (int i = 0; i < 50 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!running_.load()) break;
            
            // Record send time and send heartbeat probe
            auto send_time = std::chrono::steady_clock::now();
            heartbeat_probe_time_.store(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    send_time.time_since_epoch()).count());
            heartbeat_probe_pending_.store(true);
            
            // Send our own heartbeat (server will echo it back)
            sendProtoMessage(ProtoOAPayloadType::PROTO_HEARTBEAT_EVENT, {});
            
            // Wait for response (max 2 seconds)
            for (int i = 0; i < 20 && heartbeat_probe_pending_.load() && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (heartbeat_probe_pending_.load()) {
                std::cout << "[LATENCY] Heartbeat probe timeout\n";
            }
        }
        
        std::cout << "[LATENCY] Heartbeat probe loop stopped\n";
    }
    
    // Called when we receive a heartbeat (either server-initiated or our probe response)
    void onHeartbeatReceived() {
        auto recv_time = std::chrono::steady_clock::now();
        uint64_t recv_us = std::chrono::duration_cast<std::chrono::microseconds>(
            recv_time.time_since_epoch()).count();
        
        if (heartbeat_probe_pending_.load()) {
            uint64_t send_us = heartbeat_probe_time_.load();
            uint64_t rtt_us = recv_us - send_us;
            
            // Update stats
            latency_.heartbeat_rtt_us.store(rtt_us);
            latency_.heartbeat_sum_us.fetch_add(rtt_us);
            latency_.heartbeat_count.fetch_add(1);
            
            // Min/max
            uint64_t old_min = latency_.heartbeat_min_us.load();
            while (rtt_us < old_min && !latency_.heartbeat_min_us.compare_exchange_weak(old_min, rtt_us));
            
            uint64_t old_max = latency_.heartbeat_max_us.load();
            while (rtt_us > old_max && !latency_.heartbeat_max_us.compare_exchange_weak(old_max, rtt_us));
            
            heartbeat_probe_pending_.store(false);
            
            // Log every probe result
            std::cout << "[LATENCY] Heartbeat RTT: " << rtt_us << "us"
                      << " (avg=" << std::fixed << std::setprecision(0) << latency_.heartbeatAvgUs()
                      << " min=" << latency_.heartbeat_min_us.load()
                      << " max=" << latency_.heartbeat_max_us.load()
                      << " n=" << latency_.heartbeat_count.load() << ")\n";
        }
    }
    
    std::atomic<uint64_t> heartbeat_probe_time_{0};
    std::atomic<bool> heartbeat_probe_pending_{false};
    
    void receiveLoop() {
        std::cout << "[OpenAPI] Receive loop started\n";
        std::vector<uint8_t> buffer(65536);
        std::vector<uint8_t> pending;
        
        while (running_.load() && connected_.load()) {
            struct pollfd pfd{};
            pfd.fd = sockfd_;
            pfd.events = POLLIN;
            
            int ret = poll(&pfd, 1, 1000);
            if (ret < 0) {
                std::cerr << "[OpenAPI] Poll error: " << strerror(errno) << "\n";
                break;
            }
            if (ret == 0) continue;
            
            int n = SSL_read(ssl_, buffer.data(), buffer.size());
            if (n <= 0) {
                int err = SSL_get_error(ssl_, n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
                std::cerr << "[OpenAPI] SSL_read error: " << err << "\n";
                ERR_print_errors_fp(stderr);
                connected_.store(false);
                break;
            }
            
            std::cout << "[OpenAPI] RX " << n << " bytes\n";
            hexDump("RX raw", buffer.data(), n);
            
            pending.insert(pending.end(), buffer.begin(), buffer.begin() + n);
            
            while (pending.size() >= 4) {
                uint32_t msgLen = (static_cast<uint32_t>(pending[0]) << 24) | 
                                  (static_cast<uint32_t>(pending[1]) << 16) |
                                  (static_cast<uint32_t>(pending[2]) << 8) | 
                                  static_cast<uint32_t>(pending[3]);
                
                std::cout << "[OpenAPI] Frame: length prefix = " << msgLen << " bytes\n";
                
                if (msgLen > 1000000) {
                    std::cerr << "[OpenAPI] Invalid message length: " << msgLen << "\n";
                    pending.clear();
                    break;
                }
                
                if (pending.size() < 4 + msgLen) {
                    std::cout << "[OpenAPI] Waiting for more data\n";
                    break;
                }
                
                std::vector<uint8_t> msgData(pending.begin() + 4, pending.begin() + 4 + msgLen);
                pending.erase(pending.begin(), pending.begin() + 4 + msgLen);
                
                processMessage(msgData);
            }
        }
        
        std::cout << "[OpenAPI] Receive loop ended\n";
    }
    
    void processMessage(const std::vector<uint8_t>& data) {
        std::cout << "[OpenAPI] Processing message (" << data.size() << " bytes)\n";
        hexDump("RX Message", data.data(), data.size());
        
        ProtobufDecoder dec(data.data(), data.size());
        
        uint32_t payloadType = 0;
        std::vector<uint8_t> payload;
        std::string clientMsgId;
        
        while (dec.hasMore()) {
            auto [fieldNum, wireType] = dec.readTag();
            switch (fieldNum) {
                case 1: 
                    payloadType = dec.readVarint(); 
                    std::cout << "[OpenAPI]   Field 1 (payloadType) = " << payloadType << "\n";
                    break;
                case 2: 
                    payload = dec.readBytes(); 
                    std::cout << "[OpenAPI]   Field 2 (payload) = " << payload.size() << " bytes\n";
                    break;
                case 3: 
                    clientMsgId = dec.readString(); 
                    std::cout << "[OpenAPI]   Field 3 (clientMsgId) = \"" << clientMsgId << "\"\n";
                    break;
                default: 
                    std::cout << "[OpenAPI]   Field " << fieldNum << " (skipped, wireType=" << wireType << ")\n";
                    dec.skipField(wireType);
            }
        }
        
        std::cout << "[OpenAPI] RX payloadType=" << payloadType << "\n";
        
        switch (payloadType) {
            case ProtoOAPayloadType::PROTO_OA_APPLICATION_AUTH_RES:
                std::cout << "[OpenAPI] *** Application authenticated! ***\n";
                appAuthed_.store(true);
                break;
                
            case ProtoOAPayloadType::PROTO_OA_ACCOUNT_AUTH_RES:
                std::cout << "[OpenAPI] *** Account authenticated! ***\n";
                accountAuthed_.store(true);
                break;
                
            case ProtoOAPayloadType::PROTO_OA_SYMBOLS_LIST_RES:
                std::cout << "[OpenAPI] *** Symbols list received ***\n";
                processSymbolsList(payload);
                break;
            
            case ProtoOAPayloadType::PROTO_OA_SUBSCRIBE_SPOTS_RES:
                std::cout << "[OpenAPI] *** Subscription confirmed! ***\n";
                processSubscribeResponse(payload);
                break;
                
            case ProtoOAPayloadType::PROTO_OA_SPOT_EVENT:
                std::cout << "[OpenAPI] *** SPOT EVENT RECEIVED (" << payload.size() << " bytes) ***\n";
                processSpotEvent(payload);
                break;
                
            case ProtoOAPayloadType::PROTO_OA_ERROR_RES:
                processErrorResponse(payload);
                break;
                
            case ProtoOAPayloadType::PROTO_HEARTBEAT_EVENT:
                // Measure RTT if this is response to our probe
                onHeartbeatReceived();
                
                // If this was server-initiated (not our probe), respond
                if (!heartbeat_probe_pending_.load()) {
                    sendProtoMessage(ProtoOAPayloadType::PROTO_HEARTBEAT_EVENT, {});
                }
                break;
                
            case ProtoOAPayloadType::ERROR_RES:
                processCommonError(payload);
                break;
                
            default:
                std::cout << "[OpenAPI] Unhandled message type: " << payloadType << "\n";
        }
    }
    
    void processSymbolsList(const std::vector<uint8_t>& data) {
        std::cout << "[OpenAPI] Processing symbols list, " << data.size() << " bytes\n";
        
        if (data.empty()) {
            symbolsLoaded_.store(true);
            return;
        }
        
        // ProtoOASymbolsListRes structure:
        //   field 1: payloadType (optional)
        //   field 2: ctidTraderAccountId (int64)
        //   field 3: repeated symbol (ProtoOALightSymbol) - length-delimited
        //
        // ProtoOALightSymbol structure:
        //   field 1: symbolId (int64)
        //   field 2: symbolName (string)
        //   ... other optional fields
        
        ProtobufDecoder dec(data.data(), data.size());
        int symbolCount = 0;
        
        while (dec.hasMore()) {
            auto [fieldNum, wireType] = dec.readTag();
            
            if (fieldNum == 1 && wireType == 0) {
                // payloadType - skip
                dec.readVarint();
            }
            else if (fieldNum == 2 && wireType == 0) {
                // ctidTraderAccountId - skip
                dec.readVarint();
            }
            else if (fieldNum == 3 && wireType == 2) {
                // This is a ProtoOALightSymbol (nested message) - FIELD 3!
                std::vector<uint8_t> symbolData = dec.readBytes();
                
                // Parse the nested ProtoOALightSymbol
                ProtobufDecoder symDec(symbolData.data(), symbolData.size());
                int64_t symbolId = 0;
                std::string symbolName;
                
                while (symDec.hasMore()) {
                    auto [symFieldNum, symWireType] = symDec.readTag();
                    
                    if (symFieldNum == 1 && symWireType == 0) {
                        symbolId = symDec.readVarint();
                    }
                    else if (symFieldNum == 2 && symWireType == 2) {
                        symbolName = symDec.readString();
                    }
                    else {
                        symDec.skipField(symWireType);
                    }
                }
                
                if (symbolId > 0 && !symbolName.empty()) {
                    // Store in caches
                    symbolIdCache_[symbolName] = symbolId;
                    symbolNameCache_[symbolId] = symbolName;
                    symbolCount++;
                    
                    // Log symbols we care about
                    if (symbolName == "XAUUSD" || symbolName == "NAS100" || symbolName == "US30" ||
                        symbolName == "EURUSD" || symbolName == "GBPUSD" || symbolName == "USDJPY") {
                        std::cout << "[OpenAPI] Found symbol: " << symbolName << " -> ID " << symbolId << "\n";
                    }
                }
            }
            else {
                dec.skipField(wireType);
            }
        }
        
        std::cout << "[OpenAPI] Parsed " << symbolCount << " symbols from broker\n";
        
        // Log the symbols we need
        std::cout << "[OpenAPI] Symbol ID lookup:\n";
        for (const auto& sym : {"XAUUSD", "NAS100", "US30"}) {
            auto it = symbolIdCache_.find(sym);
            if (it != symbolIdCache_.end()) {
                std::cout << "[OpenAPI]   " << sym << " = " << it->second << "\n";
            } else {
                std::cout << "[OpenAPI]   " << sym << " = NOT FOUND!\n";
            }
        }
        
        symbolsLoaded_.store(true);
    }
    
    void processSpotEvent(const std::vector<uint8_t>& data) {
        if (data.empty()) return;
        
        // Record local receive time immediately
        auto recv_time = std::chrono::system_clock::now();
        uint64_t local_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            recv_time.time_since_epoch()).count();
        
        ProtobufDecoder dec(data.data(), data.size());
        int64_t symbolId = 0;
        int64_t bidPrice = 0, askPrice = 0;
        int64_t serverTimestamp = 0;  // Field 6 in ProtoOASpotEvent
        
        while (dec.hasMore()) {
            auto [fieldNum, wireType] = dec.readTag();
            switch (fieldNum) {
                case 2: symbolId = dec.readVarint(); break;
                case 3: bidPrice = dec.readVarint(); break;
                case 4: askPrice = dec.readVarint(); break;
                case 6: serverTimestamp = dec.readVarint(); break;  // Server timestamp (ms)
                default: dec.skipField(wireType);
            }
        }
        
        // Calculate tick latency if server timestamp available
        if (serverTimestamp > 0) {
            int64_t latency_ms = local_ms - serverTimestamp;
            if (latency_ms > 0 && latency_ms < 60000) {  // Sanity check: < 60s
                latency_.tick_latency_us.store(latency_ms * 1000);  // Convert to us
            }
        }
        
        std::string symbol;
        auto it = symbolNameCache_.find(symbolId);
        if (it != symbolNameCache_.end()) {
            symbol = it->second;
        } else {
            for (const auto& [name, id] : symbolIdCache_) {
                if (id == symbolId) { symbol = name; break; }
            }
        }
        
        if (tickCallback_ && !symbol.empty()) {
            // Get digits for this symbol (default to 5 for forex-like)
            int digits = 5;
            auto digIt = symbolDigitsCache_.find(symbolId);
            if (digIt != symbolDigitsCache_.end()) {
                digits = digIt->second;
            } else {
                // Guess based on symbol name
                if (symbol == "XAUUSD" || symbol == "GOLD") digits = 2;
                else if (symbol == "NAS100" || symbol == "US30" || symbol == "USTEC" || symbol == "DJ30") digits = 1;
                else if (symbol.find("JPY") != std::string::npos) digits = 3;
                else digits = 5;  // Default forex
            }
            
            double divisor = std::pow(10.0, digits);
            
            CTraderTick tick;
            tick.symbol = symbol;
            tick.bid = bidPrice / divisor;
            tick.ask = askPrice / divisor;
            tick.timestamp = local_ms;
            
            // Debug: Log first tick for each symbol with latency
            static std::set<std::string> loggedSymbols;
            if (loggedSymbols.find(symbol) == loggedSymbols.end()) {
                std::cout << "[OpenAPI] FIRST TICK " << symbol << ": bid=" << tick.bid 
                          << " ask=" << tick.ask << " (raw bid=" << bidPrice 
                          << " digits=" << digits;
                if (serverTimestamp > 0) {
                    std::cout << " latency=" << (local_ms - serverTimestamp) << "ms";
                }
                std::cout << ")\n";
                loggedSymbols.insert(symbol);
            }
            
            tickCallback_(tick);
        }
    }
    
    void processSubscribeResponse(const std::vector<uint8_t>& data) {
        std::cout << "[OpenAPI] Processing subscribe response (" << data.size() << " bytes)\n";
        
        // Hex dump the response
        std::cout << "[OpenAPI] Subscribe response HEX: ";
        for (auto b : data) printf("%02X ", b);
        std::cout << "\n";
        
        if (!data.empty()) {
            ProtobufDecoder dec(data.data(), data.size());
            while (dec.hasMore()) {
                auto [fieldNum, wireType] = dec.readTag();
                std::cout << "[OpenAPI]   Response field " << fieldNum << " (wire " << wireType << ")\n";
                if (fieldNum == 2 && wireType == 0) {
                    int64_t accountId = dec.readVarint();
                    std::cout << "[OpenAPI]   -> accountId = " << accountId << "\n";
                    std::cout << "[OpenAPI] ✓ Subscription confirmed for account " << accountId << "\n";
                    std::cout << "[OpenAPI] *** TICKS SHOULD NOW BE STREAMING (payloadType 2126) ***\n";
                } else {
                    uint64_t val = 0;
                    if (wireType == 0) {
                        val = dec.readVarint();
                        std::cout << "[OpenAPI]   -> value = " << val << "\n";
                    } else {
                        dec.skipField(wireType);
                    }
                }
            }
        }
    }
    
    void processErrorResponse(const std::vector<uint8_t>& data) {
        std::cout << "\n[OpenAPI] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
        std::cout << "[OpenAPI] !!! ERROR RESPONSE RECEIVED !!!\n";
        std::cout << "[OpenAPI] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
        
        if (!data.empty()) {
            hexDump("Error payload", data.data(), data.size());
        }
        
        if (data.empty()) return;
        
        ProtobufDecoder dec(data.data(), data.size());
        std::string errorCode, description;
        uint64_t responseAccountId = 0;
        
        while (dec.hasMore()) {
            auto [fieldNum, wireType] = dec.readTag();
            switch (fieldNum) {
                case 1: dec.readVarint(); break;
                case 2: responseAccountId = dec.readVarint(); break;
                case 3: errorCode = dec.readString(); break;
                case 4: description = dec.readString(); break;
                default: dec.skipField(wireType);
            }
        }
        
        std::cerr << "[OpenAPI] ERROR DETAILS:\n";
        std::cerr << "[OpenAPI]   errorCode: " << errorCode << "\n";
        std::cerr << "[OpenAPI]   description: " << description << "\n";
        std::cerr << "[OpenAPI]   response accountId: " << responseAccountId << "\n";
        std::cerr << "[OpenAPI]   OUR accountId: " << config_.accountId << "\n";
        
        if (responseAccountId != 0 && responseAccountId != static_cast<uint64_t>(config_.accountId)) {
            std::cerr << "[OpenAPI] WARNING: Response accountId doesn't match our accountId!\n";
            std::cerr << "[OpenAPI]   This might indicate a field encoding issue.\n";
        }
        
        // Check for specific errors
        if (errorCode == "INVALID_REQUEST" && description.find("not authorized") != std::string::npos) {
            std::cerr << "\n[OpenAPI] POSSIBLE CAUSES:\n";
            std::cerr << "  1. Access token expired - refresh the token\n";
            std::cerr << "  2. Account not properly authorized during auth phase\n";
            std::cerr << "  3. Wrong accountId being sent\n";
            std::cerr << "  4. Account doesn't have access to these symbols\n";
        }
    }
    
    void processCommonError(const std::vector<uint8_t>& data) {
        std::cout << "[OpenAPI] Processing COMMON ERROR (" << data.size() << " bytes)\n";
        if (!data.empty()) {
            hexDump("Common error payload", data.data(), data.size());
        }
        
        if (data.empty()) return;
        
        ProtobufDecoder dec(data.data(), data.size());
        std::string errorCode, description;
        
        while (dec.hasMore()) {
            auto [fieldNum, wireType] = dec.readTag();
            switch (fieldNum) {
                case 1: dec.readVarint(); break;
                case 2: errorCode = dec.readString(); break;
                case 3: description = dec.readString(); break;
                default: dec.skipField(wireType);
            }
        }
        
        std::cerr << "[OpenAPI] COMMON ERROR:\n";
        std::cerr << "[OpenAPI]   errorCode: " << errorCode << "\n";
        std::cerr << "[OpenAPI]   description: " << description << "\n";
    }
};

} // namespace Chimera
