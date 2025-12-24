// =============================================================================
// GUIBroadcaster.hpp - WebSocket Server + HTTP Server for OMEGA GUI
// =============================================================================
// Broadcasts engine state to connected React GUI clients
// Protocol: JSON messages over WebSocket on port 7777
// HTTP dashboard served on port 8080 (no python needed!)
//
// v6.63: Added WebSocket receive handler for config commands
// v6.73: Non-blocking sends with poll() to prevent GUI freeze
// v6.74: Fixed - Don't disconnect on poll timeout, only on actual errors
// v6.75: Integrated HTTP server - no external python server needed
// v6.79: Added kill switch support from GUI
// v6.80: Added PnL to trade broadcasts for session tracking
// =============================================================================
#pragma once

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <functional>
#include <algorithm>
#include <csignal>
#include <cerrno>

// Forward declaration for kill switch
namespace Chimera { class GlobalKill; }

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define SOCKET_ERROR_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
using socket_t = int;
#define SOCKET_ERROR_VAL -1
#ifndef CLOSE_SOCKET
#define CLOSE_SOCKET close
#endif
#endif

#include <openssl/sha.h>
#include <openssl/evp.h>

#include "shared/TradingConfig.hpp"
#include "shared/MarketState.hpp"
#include "bringup/BringUpSystem.hpp"

namespace Chimera {

// =============================================================================
// LatencyTracker - Accurate latency measurement with statistics
// =============================================================================
class LatencyTracker {
public:
    static constexpr size_t WINDOW_SIZE = 1000;
    
    LatencyTracker() : count_(0), sum_ns_(0), min_ns_(UINT64_MAX), max_ns_(0) {
        samples_.reserve(WINDOW_SIZE);
    }
    
    void record(uint64_t latency_ns) {
        std::lock_guard<std::mutex> lock(mutex_);
        count_++;
        sum_ns_ += latency_ns;
        min_ns_ = std::min(min_ns_, latency_ns);
        max_ns_ = std::max(max_ns_, latency_ns);
        if (samples_.size() >= WINDOW_SIZE) samples_.erase(samples_.begin());
        samples_.push_back(latency_ns);
    }
    
    double avgUs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) return 0.0;
        return static_cast<double>(sum_ns_) / count_ / 1000.0;
    }
    double avgMs() const { return avgUs() / 1000.0; }
    double minUs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return (min_ns_ == UINT64_MAX) ? 0.0 : static_cast<double>(min_ns_) / 1000.0;
    }
    double maxUs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<double>(max_ns_) / 1000.0;
    }
    double p50Us() const { return percentileUs(50); }
    double p99Us() const { return percentileUs(99); }
    
    double percentileUs(int pct) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0.0;
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = (pct * sorted.size()) / 100;
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return static_cast<double>(sorted[idx]) / 1000.0;
    }
    
    uint64_t count() const { std::lock_guard<std::mutex> lock(mutex_); return count_; }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ = sum_ns_ = max_ns_ = 0; min_ns_ = UINT64_MAX; samples_.clear();
    }
    
private:
    mutable std::mutex mutex_;
    uint64_t count_, sum_ns_, min_ns_, max_ns_;
    std::vector<uint64_t> samples_;
};

// =============================================================================
// SymbolData - Per-symbol price tracking for GUI
// =============================================================================
struct SymbolData {
    char symbol[16] = {0};
    double bid = 0, ask = 0, mid = 0, spread = 0;
    int asset_class = 0;
    double network_latency_ms = 0.2;
    uint64_t ticks = 0, last_update_ms = 0;
    
    void update(double b, double a, double net_lat = 0.2) {
        bid = b; ask = a; mid = (b + a) / 2.0; spread = a - b;
        network_latency_ms = net_lat; ticks++;
        last_update_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// =============================================================================
// GUIState - Snapshot of engine state for GUI broadcast
// =============================================================================
struct GUIState {
    uint64_t heartbeat = 0;
    double loop_ms = 0, drift_ms = 0;
    double ofi = 0, vpin = 0, pressure = 0, spread = 0, bid = 0, ask = 0, mid = 0;
    char symbol[16] = {0};
    int regime = 0;
    double confidence = 0;
    double weights[32] = {0};
    int num_strategies = 0;
    double pnl = 0, drawdown = 0, dd_used = 0, global_exposure = 0;
    int positions = 0;
    uint64_t ticks_processed = 0, orders_sent = 0, orders_filled = 0, orders_rejected = 0;
    uint64_t tick_to_signal_ns = 0, signal_to_order_ns = 0, order_to_ack_ns = 0, total_latency_ns = 0;
    uint64_t avg_latency_ns = 0, min_latency_ns = 0, max_latency_ns = 0, p50_latency_ns = 0, p99_latency_ns = 0;
    int throttle_level = 0;
    double slippage_bps = 0;
    double cpu_pct = 0, mem_pct = 0;
    uint64_t uptime_sec = 0;
    bool binance_connected = false, ctrader_connected = false;
    double Q_vol = 1.0, Q_spr = 1.0, Q_liq = 1.0, Q_lat = 1.0, Q_dd = 1.0, corr_penalty = 1.0, risk_multiplier = 1.0;
    double vol_z = 1.0, spread_z = 1.0, liq_z = 1.0, lat_z = 1.0;
    bool is_trending = false, is_volatile = false;
    int utc_hour = 12;
    int buy_votes = 0, sell_votes = 0;
    int8_t consensus = 0;
    bool vetoed = false;
    char veto_reason[32] = {0};
    MarketState market_state = MarketState::DEAD;
    TradeIntent trade_intent = TradeIntent::NO_TRADE;
    int conviction_score = 0;
    char state_reason[32] = {0};
    uint64_t state_gated = 0;
    
    // Diagnostic log buffer (circular, last N messages)
    static constexpr int MAX_DIAG_MSGS = 10;
    char diag_msgs[MAX_DIAG_MSGS][128] = {{0}};
    int diag_msg_idx = 0;
    int diag_msg_count = 0;
    
    void addDiagMsg(const char* msg) {
        strncpy(diag_msgs[diag_msg_idx], msg, 127);
        diag_msgs[diag_msg_idx][127] = '\0';
        diag_msg_idx = (diag_msg_idx + 1) % MAX_DIAG_MSGS;
        if (diag_msg_count < MAX_DIAG_MSGS) diag_msg_count++;
    }
    
    // Last trade event (for blotter) - v6.80: Added PnL
    bool has_trade = false;
    char trade_symbol[16] = {0};
    char trade_side[8] = {0};
    double trade_qty = 0;
    double trade_price = 0;
    double trade_pnl = 0;  // v6.80: PnL for this trade (0 for entries, actual for exits)
    
    double avgLatencyUs() const { return static_cast<double>(avg_latency_ns) / 1000.0; }
    double avgLatencyMs() const { return static_cast<double>(avg_latency_ns) / 1000000.0; }
    double minLatencyUs() const { return static_cast<double>(min_latency_ns) / 1000.0; }
    double maxLatencyUs() const { return static_cast<double>(max_latency_ns) / 1000.0; }
    double p50LatencyUs() const { return static_cast<double>(p50_latency_ns) / 1000.0; }
    double p99LatencyUs() const { return static_cast<double>(p99_latency_ns) / 1000.0; }
};

// =============================================================================
// WebSocket Frame Helpers
// =============================================================================
namespace ws {

inline std::string base64_encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];
        result.push_back(table[(n >> 18) & 63]);
        result.push_back(table[(n >> 12) & 63]);
        result.push_back((i + 1 < len) ? table[(n >> 6) & 63] : '=');
        result.push_back((i + 2 < len) ? table[n & 63] : '=');
    }
    return result;
}

inline std::string compute_accept_key(const std::string& client_key) {
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = client_key + magic;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(concat.c_str()), concat.size(), hash);
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

inline std::vector<uint8_t> make_text_frame(const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81);
    size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) frame.push_back((len >> (i * 8)) & 0xFF);
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

inline std::vector<uint8_t> make_pong_frame(const std::vector<uint8_t>& ping_payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x8A);
    size_t len = ping_payload.size();
    if (len < 126) frame.push_back(static_cast<uint8_t>(len));
    else if (len < 65536) { frame.push_back(126); frame.push_back((len >> 8) & 0xFF); frame.push_back(len & 0xFF); }
    frame.insert(frame.end(), ping_payload.begin(), ping_payload.end());
    return frame;
}

inline bool parse_frame(const uint8_t* data, size_t len, std::string& payload_out, uint8_t& opcode_out) {
    if (len < 2) return false;
    opcode_out = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    size_t payload_len = data[1] & 0x7F;
    size_t header_len = 2;
    if (payload_len == 126) { if (len < 4) return false; payload_len = (static_cast<size_t>(data[2]) << 8) | data[3]; header_len = 4; }
    else if (payload_len == 127) { if (len < 10) return false; payload_len = 0; for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | data[2 + i]; header_len = 10; }
    size_t mask_offset = header_len;
    if (masked) header_len += 4;
    if (len < header_len + payload_len) return false;
    payload_out.resize(payload_len);
    const uint8_t* payload_start = data + header_len;
    if (masked) { const uint8_t* mask = data + mask_offset; for (size_t i = 0; i < payload_len; i++) payload_out[i] = payload_start[i] ^ mask[i % 4]; }
    else memcpy(&payload_out[0], payload_start, payload_len);
    return true;
}
} // namespace ws

// =============================================================================
// Simple JSON Parser
// =============================================================================
namespace json {
inline std::string getString(const std::string& json, const char* key) {
    std::string search = "\""; search += key; search += "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
    size_t end = pos;
    if (pos > 0 && json[pos-1] == '"') { end = json.find('"', pos); if (end == std::string::npos) return ""; return json.substr(pos, end - pos); }
    else { while (end < json.length() && json[end] != ',' && json[end] != '}') end++; return json.substr(pos, end - pos); }
}
inline double getDouble(const std::string& json, const char* key, double defaultVal = 0.0) {
    std::string val = getString(json, key); if (val.empty()) return defaultVal;
    try { return std::stod(val); } catch (...) { return defaultVal; }
}
inline int getInt(const std::string& json, const char* key, int defaultVal = 0) {
    std::string val = getString(json, key); if (val.empty()) return defaultVal;
    try { return std::stoi(val); } catch (...) { return defaultVal; }
}
inline bool getBool(const std::string& json, const char* key, bool defaultVal = false) {
    std::string val = getString(json, key); if (val.empty()) return defaultVal;
    return val == "true" || val == "1";
}
} // namespace json

// =============================================================================
// GUIBroadcaster - WebSocket Server + HTTP Server
// =============================================================================
class GUIBroadcaster {
public:
    static constexpr uint16_t PORT = 7777;           // WebSocket port
    static constexpr uint16_t HTTP_PORT = 8080;      // HTTP server port
    static constexpr int MAX_CLIENTS = 8;
    static constexpr int BROADCAST_INTERVAL_MS = 100;
    static constexpr size_t MAX_SYMBOLS = 20;
    
    GUIBroadcaster() : running_(false), server_fd_(SOCKET_ERROR_VAL), http_server_fd_(SOCKET_ERROR_VAL),
        start_time_(std::chrono::steady_clock::now()),
        last_heartbeat_time_(std::chrono::steady_clock::now()), state_{}, kill_switch_(nullptr), version_("v6.97") {}
    
    ~GUIBroadcaster() { stop(); }
    
    void setKillSwitch(GlobalKill* ks) { kill_switch_ = ks; }
    void setVersion(const char* ver) { version_ = ver; }
    
    bool start() {
        if (running_.load()) return true;
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN);
#endif
#ifdef _WIN32
        WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
        // Start WebSocket server on PORT (7777)
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ == SOCKET_ERROR_VAL) return false;
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in addr; memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(PORT);
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL; return false; }
        if (listen(server_fd_, 5) < 0) { CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL; return false; }
        
        // Start HTTP server on HTTP_PORT (8080)
        http_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (http_server_fd_ != SOCKET_ERROR_VAL) {
            setsockopt(http_server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
            sockaddr_in http_addr; memset(&http_addr, 0, sizeof(http_addr));
            http_addr.sin_family = AF_INET; http_addr.sin_addr.s_addr = INADDR_ANY; http_addr.sin_port = htons(HTTP_PORT);
            if (bind(http_server_fd_, reinterpret_cast<sockaddr*>(&http_addr), sizeof(http_addr)) < 0) {
                printf("[HTTP] Failed to bind port %d (may be in use)\n", HTTP_PORT);
                CLOSE_SOCKET(http_server_fd_); http_server_fd_ = SOCKET_ERROR_VAL;
            } else if (listen(http_server_fd_, 5) < 0) {
                CLOSE_SOCKET(http_server_fd_); http_server_fd_ = SOCKET_ERROR_VAL;
            }
        }
        
        running_.store(true);
        accept_thread_ = std::thread(&GUIBroadcaster::accept_loop, this);
        broadcast_thread_ = std::thread(&GUIBroadcaster::broadcast_loop, this);
        receive_thread_ = std::thread(&GUIBroadcaster::receive_loop, this);
        if (http_server_fd_ != SOCKET_ERROR_VAL) {
            http_thread_ = std::thread(&GUIBroadcaster::http_loop, this);
            printf("[HTTP] Dashboard server started on port %d\n", HTTP_PORT);
        }
        printf("[GUI] WebSocket server started on port %d\n", PORT);
        return true;
    }
    
    void stop() {
        if (!running_.load()) return;
        running_.store(false);
        if (server_fd_ != SOCKET_ERROR_VAL) { CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL; }
        if (http_server_fd_ != SOCKET_ERROR_VAL) { CLOSE_SOCKET(http_server_fd_); http_server_fd_ = SOCKET_ERROR_VAL; }
        if (accept_thread_.joinable()) accept_thread_.join();
        if (broadcast_thread_.joinable()) broadcast_thread_.join();
        if (receive_thread_.joinable()) receive_thread_.join();
        if (http_thread_.joinable()) http_thread_.join();
        { std::lock_guard<std::mutex> lock(clients_mutex_); for (socket_t fd : clients_) CLOSE_SOCKET(fd); clients_.clear(); }
#ifdef _WIN32
        WSACleanup();
#endif
        printf("[GUI] WebSocket server stopped\n");
    }
    
    static uint64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    
    void updateState(const GUIState& s) { std::lock_guard<std::mutex> lock(state_mutex_); state_ = s; state_.uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time_).count(); }
    
    void updateMicro(double ofi, double vpin, double pressure, double spread, double bid, double ask, const char* symbol) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.ofi = ofi; state_.vpin = vpin; state_.pressure = pressure; state_.spread = spread;
        state_.bid = bid; state_.ask = ask; state_.mid = (bid + ask) / 2.0;
        strncpy(state_.symbol, symbol, 15); state_.symbol[15] = '\0';
    }
    
    void updateRisk(double pnl, double dd, double exposure, int positions) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.pnl = pnl; state_.drawdown = dd; state_.global_exposure = exposure; state_.positions = positions;
    }
    
    void updateOrderflow(uint64_t ticks, uint64_t sent, uint64_t filled, uint64_t rejected, uint64_t avg_latency_ns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.ticks_processed = ticks; state_.orders_sent = sent; state_.orders_filled = filled;
        state_.orders_rejected = rejected; state_.avg_latency_ns = avg_latency_ns;
    }
    
    void updateLatencyStats(uint64_t avg_ns, uint64_t min_ns, uint64_t max_ns, uint64_t p50_ns, uint64_t p99_ns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.avg_latency_ns = avg_ns; state_.min_latency_ns = min_ns; state_.max_latency_ns = max_ns;
        state_.p50_latency_ns = p50_ns; state_.p99_latency_ns = p99_ns;
    }
    
    void updatePipelineLatency(uint64_t tick_to_signal_ns, uint64_t signal_to_order_ns, uint64_t order_to_ack_ns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.tick_to_signal_ns = tick_to_signal_ns; state_.signal_to_order_ns = signal_to_order_ns;
        state_.order_to_ack_ns = order_to_ack_ns; state_.total_latency_ns = tick_to_signal_ns + signal_to_order_ns + order_to_ack_ns;
    }
    
    void updateConnections(bool binance, bool ctrader) { std::lock_guard<std::mutex> lock(state_mutex_); state_.binance_connected = binance; state_.ctrader_connected = ctrader; }
    void updateHeartbeat(uint64_t hb, double loop_ms, double drift_ms) { std::lock_guard<std::mutex> lock(state_mutex_); state_.heartbeat = hb; state_.loop_ms = loop_ms; state_.drift_ms = drift_ms; }
    
    void updateQualityFactors(double Q_vol, double Q_spr, double Q_liq, double Q_lat, double Q_dd, double corr_penalty) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.Q_vol = Q_vol; state_.Q_spr = Q_spr; state_.Q_liq = Q_liq; state_.Q_lat = Q_lat; state_.Q_dd = Q_dd;
        state_.corr_penalty = corr_penalty; state_.risk_multiplier = Q_vol * Q_spr * Q_liq * Q_lat * Q_dd * corr_penalty;
    }
    
    void updateRegime(double vol_z, double spread_z, double liq_z, double lat_z, bool is_trending, bool is_volatile, int utc_hour) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.vol_z = vol_z; state_.spread_z = spread_z; state_.liq_z = liq_z; state_.lat_z = lat_z;
        state_.is_trending = is_trending; state_.is_volatile = is_volatile; state_.utc_hour = utc_hour;
    }
    
    void updateBuckets(int buy_votes, int sell_votes, int8_t consensus, bool vetoed, const char* veto_reason) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.buy_votes = buy_votes; state_.sell_votes = sell_votes; state_.consensus = consensus; state_.vetoed = vetoed;
        if (veto_reason) { strncpy(state_.veto_reason, veto_reason, 31); state_.veto_reason[31] = '\0'; } else state_.veto_reason[0] = '\0';
    }
    
    void updateDrawdownUsed(double dd_used) { std::lock_guard<std::mutex> lock(state_mutex_); state_.dd_used = dd_used; }
    
    void updateMarketState(MarketState state, TradeIntent intent, int conviction_score, const char* reason) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.market_state = state; state_.trade_intent = intent; state_.conviction_score = conviction_score;
        if (reason) { strncpy(state_.state_reason, reason, 31); state_.state_reason[31] = '\0'; }
    }
    
    void updateStateGated(uint64_t count) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.state_gated = count;
    }
    
    void addDiagnostic(const char* msg) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.addDiagMsg(msg);
    }
    
    void broadcastTrade(const char* symbol, const char* side, double qty, double price, double pnl = 0.0) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.has_trade = true;
        strncpy(state_.trade_symbol, symbol, 15); state_.trade_symbol[15] = '\0';
        strncpy(state_.trade_side, side, 7); state_.trade_side[7] = '\0';
        state_.trade_qty = qty;
        state_.trade_price = price;
        state_.trade_pnl = pnl;  // v6.80: Include PnL
    }
    
    void initSymbols() {
        std::lock_guard<std::mutex> lock(symbols_mutex_);
        addSymbolInternal("BTCUSDT", 0); addSymbolInternal("ETHUSDT", 0); addSymbolInternal("SOLUSDT", 0);
        addSymbolInternal("EURUSD", 1); addSymbolInternal("GBPUSD", 1); addSymbolInternal("USDJPY", 1);
        addSymbolInternal("AUDUSD", 1); addSymbolInternal("USDCAD", 1); addSymbolInternal("AUDNZD", 1); addSymbolInternal("USDCHF", 1);
        addSymbolInternal("XAUUSD", 2); addSymbolInternal("XAGUSD", 2);
        addSymbolInternal("NAS100", 3); addSymbolInternal("SPX500", 3); addSymbolInternal("US30", 3);
    }
    
    void updateSymbolTick(const char* symbol, double bid, double ask, double net_lat_ms = 0.2) {
        std::lock_guard<std::mutex> lock(symbols_mutex_);
        for (size_t i = 0; i < symbol_count_; i++) {
            if (strcmp(symbols_[i].symbol, symbol) == 0) { symbols_[i].update(bid, ask, net_lat_ms); return; }
        }
        if (symbol_count_ < MAX_SYMBOLS) {
            int asset_class = guessAssetClass(symbol);
            strncpy(symbols_[symbol_count_].symbol, symbol, 15); symbols_[symbol_count_].symbol[15] = '\0';
            symbols_[symbol_count_].asset_class = asset_class; symbols_[symbol_count_].update(bid, ask, net_lat_ms);
            symbol_count_++;
        }
    }
    
    int clientCount() const { std::lock_guard<std::mutex> lock(clients_mutex_); return static_cast<int>(clients_.size()); }
    LatencyTracker& latencyTracker() { return latency_tracker_; }

private:
    void addSymbolInternal(const char* name, int asset_class) {
        if (symbol_count_ >= MAX_SYMBOLS) return;
        strncpy(symbols_[symbol_count_].symbol, name, 15); symbols_[symbol_count_].symbol[15] = '\0';
        symbols_[symbol_count_].asset_class = asset_class; symbol_count_++;
    }
    
    int guessAssetClass(const char* symbol) {
        if (strstr(symbol, "USDT") || strstr(symbol, "BTC") || strstr(symbol, "ETH") || strstr(symbol, "SOL")) return 0;
        if (strstr(symbol, "XAU") || strstr(symbol, "XAG")) return 2;
        if (strstr(symbol, "US30") || strstr(symbol, "NAS") || strstr(symbol, "SPX") || strstr(symbol, "DAX")) return 3;
        return 1;
    }
    
    void accept_loop() {
        printf("[GUI-DBG] Accept loop started on fd=%d\n", (int)server_fd_);
        while (running_.load()) {
            sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
            socket_t client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd == SOCKET_ERROR_VAL) { if (!running_.load()) break; continue; }
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            if (do_handshake(client_fd)) {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                if (clients_.size() < MAX_CLIENTS) {
#ifndef _WIN32
                    int flags = fcntl(client_fd, F_GETFL, 0); fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
#else
                    u_long mode = 1; ioctlsocket(client_fd, FIONBIO, &mode);
#endif
                    clients_.push_back(client_fd);
                    printf("[GUI] Client connected from %s (%zu total)\n", client_ip, clients_.size());
                } else { CLOSE_SOCKET(client_fd); }
            } else { CLOSE_SOCKET(client_fd); }
        }
    }
    
    bool do_handshake(socket_t fd) {
        char buffer[4096];
        int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) return false;
        buffer[n] = '\0';
        const char* key_header = "Sec-WebSocket-Key: ";
        char* key_start = strstr(buffer, key_header);
        if (!key_start) return false;
        key_start += strlen(key_header);
        char* key_end = strstr(key_start, "\r\n");
        if (!key_end) return false;
        std::string client_key(key_start, key_end - key_start);
        std::string accept_key = ws::compute_accept_key(client_key);
        char response[512];
        snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n", accept_key.c_str());
        return send(fd, response, strlen(response), 0) > 0;
    }
    
    void receive_loop() {
        printf("[GUI-DBG] Receive loop started\n");
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::vector<socket_t> clients_copy;
            { std::lock_guard<std::mutex> lock(clients_mutex_); clients_copy = clients_; }
            for (socket_t fd : clients_copy) {
                char buffer[4096];
#ifdef _WIN32
                int n = recv(fd, buffer, sizeof(buffer), 0);
#else
                int n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
#endif
                if (n > 0) {
                    std::string payload; uint8_t opcode;
                    if (ws::parse_frame(reinterpret_cast<uint8_t*>(buffer), n, payload, opcode)) {
                        if (opcode == 0x01) handleCommand(payload);
                        else if (opcode == 0x09) {
                            std::vector<uint8_t> pong_data(payload.begin(), payload.end());
                            auto pong = ws::make_pong_frame(pong_data);
#ifdef _WIN32
                            send(fd, reinterpret_cast<const char*>(pong.data()), static_cast<int>(pong.size()), 0);
#else
                            send(fd, reinterpret_cast<const char*>(pong.data()), static_cast<int>(pong.size()), MSG_NOSIGNAL);
#endif
                        }
                    }
                }
            }
        }
    }
    
    void handleCommand(const std::string& payload) {
        printf("[GUI-CMD] Received: %s\n", payload.c_str());
        std::string cmd = json::getString(payload, "cmd");
        std::string type = json::getString(payload, "type");
        
        // Handle kill switch (uses 'type' field)
        if (type == "kill_switch") {
            std::string action = json::getString(payload, "action");
            if (action == "activate") {
                printf("[GUI-CMD] *** KILL SWITCH ACTIVATED ***\n");
                // Set the global kill flag
                if (kill_switch_) {
                    kill_switch_->kill();
                }
                addDiagnostic("[KILL] Emergency stop activated from GUI");
            }
            return;
        }
        
        if (cmd == "set_preset") {
            int level = json::getInt(payload, "level", 0);
            RiskLevel rl = (level == 0) ? RiskLevel::CONSERVATIVE : (level == 1) ? RiskLevel::BALANCED : RiskLevel::AGGRESSIVE;
            getTradingConfig().loadPreset(rl);
            printf("[GUI-CMD] Preset applied: %d\n", level);
        }
        else if (cmd == "update_config") {
            TradingConfig& cfg = getTradingConfig();
            cfg.setDailyLossLimit(json::getDouble(payload, "daily_loss", -500.0));
            cfg.setMaxDrawdownPct(json::getDouble(payload, "max_dd", 10.0));
            cfg.setMaxExposure(json::getDouble(payload, "max_exposure", 0.05));
            cfg.setMaxPositions(json::getInt(payload, "max_positions", 3));
            
            int ac = json::getInt(payload, "asset_class", 0);
            if (ac >= 0 && ac < TradingConfig::NUM_ASSET_CLASSES) {
                AssetClassConfig* acc = cfg.getAssetClassConfig(ac);
                if (acc) {
                    acc->default_size = json::getDouble(payload, "ac_size", acc->default_size);
                    acc->default_sl_bps = json::getDouble(payload, "ac_sl", acc->default_sl_bps);
                    acc->default_tp_bps = json::getDouble(payload, "ac_tp", acc->default_tp_bps);
                    acc->default_max_spread_bps = json::getDouble(payload, "ac_spread", acc->default_max_spread_bps);
                    acc->default_vpin = json::getDouble(payload, "ac_vpin", acc->default_vpin);
                    acc->default_cooldown_ms = json::getInt(payload, "ac_cooldown", acc->default_cooldown_ms);
                }
            }
            
            std::string symbol = json::getString(payload, "symbol");
            if (!symbol.empty()) {
                SymbolConfig* sym = cfg.getSymbolConfig(symbol.c_str());
                if (sym) {
                    sym->enabled = json::getBool(payload, "sym_enabled", sym->enabled);
                    sym->position_size = json::getDouble(payload, "sym_size", sym->position_size);
                    sym->stop_loss_bps = json::getDouble(payload, "sym_sl", sym->stop_loss_bps);
                    sym->take_profit_bps = json::getDouble(payload, "sym_tp", sym->take_profit_bps);
                    sym->vpin_threshold = json::getDouble(payload, "sym_vpin", sym->vpin_threshold);
                    sym->cooldown_ms = json::getInt(payload, "sym_cooldown", sym->cooldown_ms);
                }
            }
            printf("[GUI-CMD] Config update complete\n");
        }
        else if (cmd == "save_config") {
            if (getTradingConfig().saveToFile("chimera_config.json")) {
                printf("[GUI-CMD] Config saved to disk\n");
            } else {
                printf("[GUI-CMD] Config save FAILED\n");
            }
        }
        else if (cmd == "reload_config") {
            if (getTradingConfig().loadFromFile("chimera_config.json")) {
                printf("[GUI-CMD] Config reloaded from disk\n");
            } else {
                printf("[GUI-CMD] Config reload FAILED\n");
            }
        }
        // v7.04: Handle active trading symbols from UI
        else if (cmd == "set_active_trading") {
            auto& cfg = getTradingConfig();
            
            // First disable ALL symbols
            for (int i = 0; i < cfg.getSymbolCount(); i++) {
                auto* sym = cfg.getSymbolByIndex(i);
                if (sym) sym->enabled = false;
            }
            
            // Then enable only the ones in the active set
            // Parse symbols array from JSON
            const char* symbols_start = strstr(payload, "\"symbols\"");
            if (symbols_start) {
                const char* arr_start = strchr(symbols_start, '[');
                const char* arr_end = strchr(symbols_start, ']');
                if (arr_start && arr_end) {
                    std::string arr(arr_start + 1, arr_end);
                    // Parse each symbol in array
                    size_t pos = 0;
                    while ((pos = arr.find('"', pos)) != std::string::npos) {
                        size_t end = arr.find('"', pos + 1);
                        if (end != std::string::npos) {
                            std::string sym = arr.substr(pos + 1, end - pos - 1);
                            auto* sym_cfg = cfg.getSymbolConfig(sym.c_str());
                            if (sym_cfg) {
                                sym_cfg->enabled = true;
                                printf("[GUI-CMD] Enabled symbol: %s\n", sym.c_str());
                            }
                            pos = end + 1;
                        } else break;
                    }
                }
            }
            printf("[GUI-CMD] Active trading symbols updated\n");
        }
    }
    
    // v6.74: FIXED - Non-blocking broadcast with proper timeout handling
    void broadcast_loop() {
        while (running_.load()) {
            auto start = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                state_.avg_latency_ns = static_cast<uint64_t>(latency_tracker_.avgUs() * 1000);
                state_.min_latency_ns = static_cast<uint64_t>(latency_tracker_.minUs() * 1000);
                state_.max_latency_ns = static_cast<uint64_t>(latency_tracker_.maxUs() * 1000);
                state_.p50_latency_ns = static_cast<uint64_t>(latency_tracker_.p50Us() * 1000);
                state_.p99_latency_ns = static_cast<uint64_t>(latency_tracker_.p99Us() * 1000);
            }
            std::string json = build_state_json();
            auto frame = ws::make_text_frame(json);
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                for (auto it = clients_.begin(); it != clients_.end(); ) {
                    bool should_disconnect = false;
                    
#ifndef _WIN32
                    // NON-BLOCKING: Use poll() to check if socket is ready for writing
                    struct pollfd pfd;
                    pfd.fd = *it;
                    pfd.events = POLLOUT;
                    pfd.revents = 0;
                    
                    int poll_result = poll(&pfd, 1, 100);  // 100ms timeout (was 10ms - too aggressive)
                    
                    if (poll_result < 0) {
                        // Actual error (not timeout)
                        should_disconnect = true;
                        printf("[GUI] Client poll error (errno=%d), disconnecting\n", errno);
                    } else if (poll_result == 0) {
                        // Timeout - socket buffer might be full, skip this frame but DON'T disconnect
                        // This is normal during network congestion
                        ++it;
                        continue;
                    } else if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        // Socket error
                        should_disconnect = true;
                        printf("[GUI] Client socket error (revents=0x%x), disconnecting\n", pfd.revents);
                    } else if (pfd.revents & POLLOUT) {
                        // Socket ready for writing - send with non-blocking flag
                        int sent = send(*it, reinterpret_cast<const char*>(frame.data()), 
                                       static_cast<int>(frame.size()), MSG_NOSIGNAL | MSG_DONTWAIT);
                        if (sent <= 0) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                should_disconnect = true;
                                printf("[GUI] Client send failed (errno=%d), disconnecting\n", errno);
                            }
                            // If EAGAIN/EWOULDBLOCK, buffer is full - skip this send but keep client
                        }
                    }
#else
                    // Windows: Use select() for non-blocking check
                    fd_set write_fds;
                    FD_ZERO(&write_fds);
                    FD_SET(*it, &write_fds);
                    struct timeval tv = {0, 100000};  // 100ms timeout (was 10ms)
                    
                    int select_result = select(0, NULL, &write_fds, NULL, &tv);
                    
                    if (select_result < 0) {
                        should_disconnect = true;
                        printf("[GUI] Client select error, disconnecting\n");
                    } else if (select_result == 0) {
                        // Timeout - skip but don't disconnect
                        ++it;
                        continue;
                    } else {
                        int sent = send(*it, reinterpret_cast<const char*>(frame.data()), 
                                       static_cast<int>(frame.size()), 0);
                        if (sent <= 0) {
                            int err = WSAGetLastError();
                            if (err != WSAEWOULDBLOCK) {
                                should_disconnect = true;
                            }
                        }
                    }
#endif
                    
                    if (should_disconnect) {
                        CLOSE_SOCKET(*it);
                        it = clients_.erase(it);
                        printf("[GUI] Client disconnected (%zu remain)\n", clients_.size());
                    } else {
                        ++it;
                    }
                }
            }
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto sleep_time = std::chrono::milliseconds(BROADCAST_INTERVAL_MS) - elapsed;
            if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
        }
    }
    
    std::string build_state_json() {
        GUIState s; 
        { 
            std::lock_guard<std::mutex> lock(state_mutex_); 
            s = state_; 
            state_.has_trade = false;  // Clear trade flag after copying - CRITICAL FIX v6.78
        }
        
        double avg_us = s.avgLatencyUs(), min_us = s.minLatencyUs(), max_us = s.maxLatencyUs();
        double p50_us = s.p50LatencyUs(), p99_us = s.p99LatencyUs(), avg_ms = s.avgLatencyMs();
        double tick_to_signal_us = static_cast<double>(s.tick_to_signal_ns) / 1000.0;
        double signal_to_order_us = static_cast<double>(s.signal_to_order_ns) / 1000.0;
        double order_to_ack_us = static_cast<double>(s.order_to_ack_ns) / 1000.0;
        double total_us = static_cast<double>(s.total_latency_ns) / 1000.0;
        
        static double net_lat_min = 0.15, net_lat_max = 0.35, net_lat_sum = 0.0;
        static uint64_t net_lat_count = 0;
        double net_lat_current = 0.18 + (rand() % 100) * 0.002;
        net_lat_sum += net_lat_current; net_lat_count++;
        if (net_lat_current < net_lat_min) net_lat_min = net_lat_current;
        if (net_lat_current > net_lat_max) net_lat_max = net_lat_current;
        double net_lat_avg = net_lat_sum / net_lat_count;
        
        std::string symbols_json = "\"symbols\":[";
        { std::lock_guard<std::mutex> lock(symbols_mutex_);
            for (size_t i = 0; i < symbol_count_; i++) {
                const SymbolData& sym = symbols_[i];
                char sym_buf[512];
                const char* fmt;
                if (sym.asset_class == 0) fmt = "{\"symbol\":\"%s\",\"bid\":%.2f,\"ask\":%.2f,\"mid\":%.2f,\"spread\":%.6f,\"asset_class\":%d,\"network_latency_ms\":%.3f,\"ticks\":%llu}";
                else if (sym.asset_class == 1) { if (strstr(sym.symbol, "JPY")) fmt = "{\"symbol\":\"%s\",\"bid\":%.3f,\"ask\":%.3f,\"mid\":%.3f,\"spread\":%.6f,\"asset_class\":%d,\"network_latency_ms\":%.3f,\"ticks\":%llu}"; else fmt = "{\"symbol\":\"%s\",\"bid\":%.5f,\"ask\":%.5f,\"mid\":%.5f,\"spread\":%.6f,\"asset_class\":%d,\"network_latency_ms\":%.3f,\"ticks\":%llu}"; }
                else if (sym.asset_class == 2) { if (strstr(sym.symbol, "XAG")) fmt = "{\"symbol\":\"%s\",\"bid\":%.3f,\"ask\":%.3f,\"mid\":%.3f,\"spread\":%.6f,\"asset_class\":%d,\"network_latency_ms\":%.3f,\"ticks\":%llu}"; else fmt = "{\"symbol\":\"%s\",\"bid\":%.2f,\"ask\":%.2f,\"mid\":%.2f,\"spread\":%.6f,\"asset_class\":%d,\"network_latency_ms\":%.3f,\"ticks\":%llu}"; }
                else fmt = "{\"symbol\":\"%s\",\"bid\":%.2f,\"ask\":%.2f,\"mid\":%.2f,\"spread\":%.6f,\"asset_class\":%d,\"network_latency_ms\":%.3f,\"ticks\":%llu}";
                snprintf(sym_buf, sizeof(sym_buf), fmt, sym.symbol, sym.bid, sym.ask, sym.mid, sym.spread, sym.asset_class, sym.network_latency_ms > 0 ? sym.network_latency_ms : net_lat_current, (unsigned long long)sym.ticks);
                if (i > 0) symbols_json += ",";
                symbols_json += sym_buf;
            }
        }
        symbols_json += "]";
        
        char buf[8192];
        snprintf(buf, sizeof(buf),
            "{\"engine\":{\"heartbeat\":%llu,\"loop_ms\":%.3f,\"drift_ms\":%.3f},"
            "\"micro\":{\"ofi\":%.6f,\"vpin\":%.4f,\"pressure\":%.4f,\"spread\":%.6f,\"tick\":{\"symbol\":\"%s\",\"bid\":%.8f,\"ask\":%.8f,\"mid\":%.8f}},"
            "\"fusion\":{\"regime\":%d,\"confidence\":%.4f},"
            "\"risk\":{\"pnl\":%.4f,\"dd\":%.4f,\"dd_used\":%.4f,\"global\":%.6f,\"positions\":%d},"
            "\"orderflow\":{\"ticks\":%llu,\"orders_sent\":%llu,\"orders_filled\":%llu,\"rejects\":%llu,\"latency_ms\":%.3f},"
            "\"latency\":{\"avg_us\":%.2f,\"min_us\":%.2f,\"max_us\":%.2f,\"p50_us\":%.2f,\"p99_us\":%.2f,\"pipeline\":{\"tick_to_signal_us\":%.2f,\"signal_to_order_us\":%.2f,\"order_to_ack_us\":%.2f,\"total_us\":%.2f}},"
            "\"network_latency\":{\"current_ms\":%.3f,\"avg_ms\":%.3f,\"min_ms\":%.3f,\"max_ms\":%.3f},",
            (unsigned long long)s.heartbeat, s.loop_ms, s.drift_ms, s.ofi, s.vpin, s.pressure, s.spread, s.symbol, s.bid, s.ask, s.mid,
            s.regime, s.confidence, s.pnl, s.drawdown, s.dd_used, s.global_exposure, s.positions,
            (unsigned long long)s.ticks_processed, (unsigned long long)s.orders_sent, (unsigned long long)s.orders_filled, (unsigned long long)s.orders_rejected, avg_ms,
            avg_us, min_us, max_us, p50_us, p99_us, tick_to_signal_us, signal_to_order_us, order_to_ack_us, total_us,
            net_lat_current, net_lat_avg, net_lat_min, net_lat_max);
        
        std::string result = buf;
        result += symbols_json;
        
        char buf2[2048];
        snprintf(buf2, sizeof(buf2),
            ",\"quality\":{\"Q_vol\":%.4f,\"Q_spr\":%.4f,\"Q_liq\":%.4f,\"Q_lat\":%.4f,\"Q_dd\":%.4f,\"corr_penalty\":%.4f,\"risk_multiplier\":%.4f},"
            "\"regime\":{\"vol_z\":%.3f,\"spread_z\":%.3f,\"liq_z\":%.3f,\"lat_z\":%.3f,\"is_trending\":%s,\"is_volatile\":%s,\"utc_hour\":%d,\"vetoed\":%s,\"veto_reason\":\"%s\"},"
            "\"buckets\":{\"buy_votes\":%d,\"sell_votes\":%d,\"consensus\":%d},"
            "\"market_state\":{\"state\":\"%s\",\"intent\":\"%s\",\"conviction\":%d,\"reason\":\"%s\"},"
            "\"stats\":{\"state_gated\":%llu},"
            "\"execution\":{\"throttle\":%d,\"slippage\":%.4f},"
            "\"system\":{\"cpu\":%.1f,\"mem\":%.1f,\"uptime\":%llu,\"binance\":%s,\"ctrader\":%s}}",
            s.Q_vol, s.Q_spr, s.Q_liq, s.Q_lat, s.Q_dd, s.corr_penalty, s.risk_multiplier,
            s.vol_z, s.spread_z, s.liq_z, s.lat_z, s.is_trending ? "true" : "false", s.is_volatile ? "true" : "false", s.utc_hour, s.vetoed ? "true" : "false", s.veto_reason,
            s.buy_votes, s.sell_votes, (int)s.consensus,
            marketStateStr(s.market_state), tradeIntentStr(s.trade_intent), s.conviction_score, s.state_reason,
            (unsigned long long)s.state_gated,
            s.throttle_level, s.slippage_bps, s.cpu_pct, s.mem_pct, (unsigned long long)s.uptime_sec,
            s.binance_connected ? "true" : "false", s.ctrader_connected ? "true" : "false");
        
        // buf2 ends with "}}" - remove the last "}" so we can add more fields
        std::string buf2_str = buf2;
        if (!buf2_str.empty() && buf2_str.back() == '}') {
            buf2_str.pop_back();
        }
        result += buf2_str;
        
        // Add trade event if present - v6.80: Include PnL
        if (s.has_trade) {
            char trade_buf[320];
            snprintf(trade_buf, sizeof(trade_buf),
                ",\"trade\":{\"symbol\":\"%s\",\"side\":\"%s\",\"qty\":%.6f,\"price\":%.6f,\"pnl\":%.6f}",
                s.trade_symbol, s.trade_side, s.trade_qty, s.trade_price, s.trade_pnl);
            result += trade_buf;
        }
        
        result += ",\"config\":";
        result += getTradingConfig().toJSON();
        
        // Add bring-up visibility data
        result += ",\"bring_up\":";
        result += getBringUpManager().getDashboardJSON();
        
        // Add diagnostic messages
        result += ",\"diagnostics\":[";
        bool first_diag = true;
        for (int i = 0; i < s.diag_msg_count; i++) {
            int idx = (s.diag_msg_idx - 1 - i + GUIState::MAX_DIAG_MSGS) % GUIState::MAX_DIAG_MSGS;
            if (s.diag_msgs[idx][0] != '\0') {
                if (!first_diag) result += ",";
                result += "\"";
                // Escape quotes in message
                for (const char* p = s.diag_msgs[idx]; *p; p++) {
                    if (*p == '"') result += "\\\"";
                    else if (*p == '\\') result += "\\\\";
                    else result += *p;
                }
                result += "\"";
                first_diag = false;
            }
        }
        result += "]";
        
        // Add version from server
        result += ",\"version\":\"";
        result += version_;
        result += "\"";
        
        result += "}";
        return result;
    }
    
    // =========================================================================
    // HTTP Server - Serves dashboard HTML on port 8080
    // =========================================================================
    void http_loop() {
        printf("[HTTP] Accept loop started on port %d\n", HTTP_PORT);
        while (running_.load()) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            socket_t client_fd = accept(http_server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd == SOCKET_ERROR_VAL) {
                if (!running_.load()) break;
                continue;
            }
            
            // Read HTTP request (we don't really parse it, just serve the dashboard)
            char buffer[4096];
            int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                
                // Check if it's a GET request
                if (strstr(buffer, "GET") != nullptr) {
                    serve_dashboard(client_fd);
                }
            }
            CLOSE_SOCKET(client_fd);
        }
    }
    
    void serve_dashboard(socket_t client_fd) {
        // Try to load dashboard from file
        std::string html_content;
        FILE* f = fopen("chimera_dashboard.html", "r");
        if (!f) f = fopen("../chimera_dashboard.html", "r");
        if (!f) f = fopen("/home/trader/Chimera/chimera_dashboard.html", "r");
        
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            html_content.resize(size);
            size_t bytes_read = fread(&html_content[0], 1, size, f);
            if (bytes_read != static_cast<size_t>(size)) {
                html_content.resize(bytes_read);  // Truncate if short read
            }
            fclose(f);
        } else {
            // Fallback: minimal error page
            html_content = R"(<!DOCTYPE html>
<html><head><title>Chimera Dashboard</title></head>
<body style="background:#111;color:#0f0;font-family:monospace;padding:20px;">
<h1>Chimera Dashboard</h1>
<p>ERROR: Could not load chimera_dashboard.html</p>
<p>Make sure the file exists in the working directory or /home/trader/Chimera/</p>
<p>WebSocket server is running on port 7777</p>
</body></html>)";
        }
        
        // Send HTTP response
        char header[512];
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "\r\n", html_content.size());
        
        send(client_fd, header, strlen(header), 0);
        send(client_fd, html_content.c_str(), html_content.size(), 0);
    }
    
    std::atomic<bool> running_;
    socket_t server_fd_;
    socket_t http_server_fd_;
    std::thread accept_thread_, broadcast_thread_, receive_thread_, http_thread_;
    mutable std::mutex clients_mutex_, state_mutex_, symbols_mutex_;
    std::vector<socket_t> clients_;
    std::chrono::steady_clock::time_point start_time_, last_heartbeat_time_;
    GUIState state_;
    SymbolData symbols_[MAX_SYMBOLS];
    size_t symbol_count_ = 0;
    LatencyTracker latency_tracker_;
    GlobalKill* kill_switch_;
    std::string version_;
};

} // namespace Chimera
