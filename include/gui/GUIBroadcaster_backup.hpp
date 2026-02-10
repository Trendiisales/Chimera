// =============================================================================
// GUIBroadcaster.hpp - WebSocket Server + HTTP Server for OMEGA GUI
// =============================================================================
// Broadcasts engine state to connected React GUI clients
// Protocol: JSON messages over WebSocket on port 7777
// HTTP dashboard served on port 8080 (no python needed!)
//
// v4.9.26: LATENCY FIX - Removed fake network_latency cruft
//          - hot_path_latency is the ONLY real latency source now
//          - Cleaned up field naming
//          - Removed fake random latency generation
//          - Added clear diagnostics when no latency data
// v3.12: Version now set properly from main_dual.cpp via setVersion()
// v3.11: STATIC VARIABLE AUDIT - removed mutable statics for latency tracking
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
#include <sys/ioctl.h>
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
#include "shared/SymbolEnabledManager.hpp"  // v4.3.2: Global symbol enable/disable
#include "bringup/BringUpSystem.hpp"
#include "core/EngineOwnership.hpp"  // v4.5.1: NAS100 ownership tracking
#include "shared/GlobalRiskGovernor.hpp"  // v4.5.1: Risk governor

namespace Chimera {

// =============================================================================
// LatencyTracker - General purpose latency measurement (NOT hot-path order latency)
// NOTE: This is SEPARATE from HotPathLatencyTracker which tracks order send→ACK
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

// v7.12: Expectancy metrics per symbol for GUI display
struct SymbolExpectancy {
    char symbol[16] = {0};
    int trades = 0;
    double expectancy_bps = 0.0;
    double win_rate = 0.0;
    double flip_rate = 0.0;
    double avg_hold_ms = 0.0;
    bool disabled = false;
    char disable_reason[32] = {0};
};

// v3.0: Expectancy Health Panel data
struct ExpectancyHealthRow {
    char symbol[16] = {0};
    char regime[16] = "UNKNOWN";       // CLEAN, NOISY, DECAY, TOXIC
    double expectancy_bps = 0.0;
    double slope = 0.0;
    double slope_delta = 0.0;
    double divergence_bps = 0.0;
    int divergence_streak = 0;
    char session[8] = "N";             // A, L, N, O
    char state[8] = "OFF";             // LIVE, WARN, PAUSE, OFF, SHADOW
    char pause_reason[24] = "";        // v4.2.3: Increased from 8 to 24 for full block reasons
};

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
    bool ctrader_connected = false;
    uint32_t fix_reconnects = 0;  // Track FIX reconnection attempts
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
    
    // v7.12: Expectancy data for GUI panel
    static constexpr int MAX_EXPECTANCY_SYMBOLS = 16;
    SymbolExpectancy expectancy[MAX_EXPECTANCY_SYMBOLS];
    int expectancy_count = 0;
    
    // v3.0: Expectancy Health Panel data
    ExpectancyHealthRow health[MAX_EXPECTANCY_SYMBOLS];
    int health_count = 0;
    
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
    
    // Last trade event (for blotter) - v6.80: Added PnL, v4.9.1: Added engine/strategy
    bool has_trade = false;
    char trade_symbol[16] = {0};
    char trade_side[8] = {0};
    double trade_qty = 0;
    double trade_price = 0;
    double trade_pnl = 0;  // v6.80: PnL for this trade (0 for entries, actual for exits)
    uint8_t trade_engine = 255;    // v4.12.0: EngineId (0=CFD, 1=INCOME)
    uint8_t trade_strategy = 255;  // v4.9.1: StrategyId
    
    // v4.9.1: Connection alert state
    bool connection_alert = false;
    char connection_alert_msg[64] = {0};
    uint64_t last_connection_alert_time = 0;
    
    // v4.6.0: ML Feature Logger stats
    uint64_t ml_features_logged = 0;
    uint64_t ml_trades_logged = 0;
    uint64_t ml_records_written = 0;
    uint64_t ml_records_dropped = 0;
    
    // v4.6.0: ML Gate stats
    uint64_t ml_gate_accepts = 0;
    uint64_t ml_gate_rejects = 0;
    double ml_gate_accept_rate = 0.0;
    
    // v4.6.0: ML Drift Guard stats
    double ml_rolling_q50 = 0.0;
    double ml_rolling_q10 = 0.0;
    bool ml_drift_kill = false;
    bool ml_drift_throttle = false;
    
    // v4.6.0: ML Venue Router stats
    uint64_t ml_venue_fix = 0;
    uint64_t ml_venue_cfd = 0;
    
    // v4.9.8: Governor Heat telemetry (per-symbol)
    struct GovernorHeatData {
        double heat = 0.0;
        double size_mult = 1.0;
        char state[16] = "NORMAL";
    };
    GovernorHeatData gov_heat_btc;
    GovernorHeatData gov_heat_eth;
    GovernorHeatData gov_heat_sol;
    
    // v4.9.10: Hot-path order latency (send → ACK) - HONEST metrics from HotPathLatencyTracker
    // THIS IS THE ONLY REAL LATENCY SOURCE - wired from OrderSender via main
    double hot_path_min_ms = 0.0;
    double hot_path_p10_ms = 0.0;
    double hot_path_p50_ms = 0.0;
    double hot_path_p90_ms = 0.0;
    double hot_path_p99_ms = 0.0;
    uint64_t hot_path_samples = 0;
    uint64_t hot_path_spikes = 0;
    char hot_path_state[16] = "NO_DATA";   // v4.9.26: Default to NO_DATA for clarity
    char hot_path_exec_mode[16] = "NO_TRADE";
    
    // v4.9.34: CFD FIX latency (order send → ACK) - CO-LOCATED EDGE
    // This is the REAL co-lo latency from London VPS to cTrader
    double cfd_lat_min_ms = 0.0;
    double cfd_lat_avg_ms = 0.0;
    double cfd_lat_max_ms = 0.0;
    double cfd_lat_p50_ms = 0.0;
    double cfd_lat_p99_ms = 0.0;
    uint64_t cfd_lat_samples = 0;
    char cfd_lat_state[16] = "NO_DATA";
    
    // v4.9.10: Bootstrap system mode
    char system_mode[16] = "BOOTSTRAP";
    uint32_t probes_sent = 0;
    uint32_t probes_acked = 0;
    
    // =========================================================================
    // v4.9.12: REGIME × ALPHA × BROKER DATA
    // =========================================================================
    
    // Regime × Alpha cell data
    struct RegimeAlphaCell {
        char broker[16] = {0};
        char regime[16] = {0};
        char alpha[16] = {0};
        double net_r = 0.0;
        int trades = 0;
        double win_rate = 0.0;
        double sharpe = 0.0;
        double fill_rate = 0.0;
        double reject_rate = 0.0;
        double avg_latency_ms = 0.0;
        double slippage_bps = 0.0;
        double gross_edge_bps = 0.0;
        double spread_paid_bps = 0.0;
        double latency_cost_bps = 0.0;
        char status[16] = "ACTIVE";  // ACTIVE, MONITOR, RETIRED, HALT
        double hourly_exp[24] = {0};  // Hourly expectancy curve
        int hourly_trades[24] = {0};
    };
    static constexpr int MAX_REGIME_ALPHA_CELLS = 64;  // 4 regimes × 4 alphas × 4 brokers
    RegimeAlphaCell regime_alpha_cells[MAX_REGIME_ALPHA_CELLS];
    int regime_alpha_count = 0;
    
    // Auto-retirement events
    struct RetirementEvent {
        char alpha[16] = {0};
        char regime[16] = {0};
        char broker[16] = {0};
        char reason[64] = {0};
        uint64_t timestamp_ms = 0;
    };
    static constexpr int MAX_RETIREMENT_EVENTS = 20;
    RetirementEvent retirement_events[MAX_RETIREMENT_EVENTS];
    int retirement_event_count = 0;
    
    // No-trade reason aggregates
    struct NoTradeReasonAgg {
        char reason[32] = {0};
        int count = 0;
        double pct = 0.0;
    };
    static constexpr int MAX_NO_TRADE_REASONS = 16;
    NoTradeReasonAgg no_trade_reasons[MAX_NO_TRADE_REASONS];
    int no_trade_reason_count = 0;
    
    // Physics state
    char physics_state[16] = "WAN";  // WAN, NEAR_COLO, COLO
    
    // v4.9.25: Execution Governor state (for visibility into FROZEN state)
    char venue_state[24] = "UNKNOWN";        // HEALTHY, DEGRADED, HALTED, RECOVERY_COOLDOWN
    bool execution_frozen = false;           // True if any symbol is frozen
    char frozen_symbols[64] = "";            // Comma-separated list of frozen symbols
    uint32_t consecutive_failures = 0;       // For debugging connectivity issues
    uint64_t signature_rejections = 0;       // v4.9.27: -1022 signature failures
    
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
// Uptime Formatting Helper
// =============================================================================
inline std::string formatUptime(uint64_t total_sec) {
    uint64_t days = total_sec / 86400;
    uint64_t hours = (total_sec % 86400) / 3600;
    uint64_t minutes = (total_sec % 3600) / 60;
    uint64_t seconds = total_sec % 60;
    
    char buf[64];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%llud %lluh %llum", 
                 (unsigned long long)days, (unsigned long long)hours, (unsigned long long)minutes);
    } else if (hours > 0) {
        snprintf(buf, sizeof(buf), "%lluh %llum %llus", 
                 (unsigned long long)hours, (unsigned long long)minutes, (unsigned long long)seconds);
    } else if (minutes > 0) {
        snprintf(buf, sizeof(buf), "%llum %llus", 
                 (unsigned long long)minutes, (unsigned long long)seconds);
    } else {
        snprintf(buf, sizeof(buf), "%llus", (unsigned long long)seconds);
    }
    return buf;
}

// =============================================================================
// GUIBroadcaster - WebSocket Server + HTTP Server
// =============================================================================
class GUIBroadcaster {
public:
    static constexpr uint16_t PORT = 7777;           // WebSocket port
    static constexpr uint16_t HTTP_PORT = 8080;      // HTTP server port
    static constexpr int MAX_CLIENTS = 8;
    static constexpr int BROADCAST_INTERVAL_MS = 100;
    static constexpr size_t MAX_SYMBOLS = 30;  // 8 forex + 2 metals + 5 indices + buffer
    
    GUIBroadcaster() : running_(false), server_fd_(SOCKET_ERROR_VAL), http_server_fd_(SOCKET_ERROR_VAL),
                       symbol_count_(0), kill_switch_(nullptr) {}
    ~GUIBroadcaster() { stop(); }
    
    // Prevent copy
    GUIBroadcaster(const GUIBroadcaster&) = delete;
    GUIBroadcaster& operator=(const GUIBroadcaster&) = delete;
    
    void setKillSwitch(GlobalKill* ks) { kill_switch_ = ks; }
    
    void setVersion(const std::string& version) { version_ = version; }
    
    // v4.31.0: Bridge ExecutionMetrics → GUI latency display
    void setExecutionLatencyMs(double ms) { execution_latency_ms_ = ms; }
    
    bool start() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            printf("[GUI] WSAStartup failed\n");
            return false;
        }
#endif
        
        // Create WebSocket server socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ == SOCKET_ERROR_VAL) {
            printf("[GUI] Failed to create WebSocket socket\n");
            return false;
        }
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(PORT);
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            printf("[GUI] Failed to bind WebSocket port %d\n", PORT);
            CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL;
            return false;
        }
        listen(server_fd_, 5);
        
        // Create HTTP server socket
        http_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (http_server_fd_ == SOCKET_ERROR_VAL) {
            printf("[GUI] Failed to create HTTP socket\n");
            CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL;
            return false;
        }
        setsockopt(http_server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in http_addr{}; http_addr.sin_family = AF_INET; http_addr.sin_addr.s_addr = INADDR_ANY; http_addr.sin_port = htons(HTTP_PORT);
        if (bind(http_server_fd_, reinterpret_cast<sockaddr*>(&http_addr), sizeof(http_addr)) < 0) {
            printf("[GUI] Failed to bind HTTP port %d\n", HTTP_PORT);
            CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL;
            CLOSE_SOCKET(http_server_fd_); http_server_fd_ = SOCKET_ERROR_VAL;
            return false;
        }
        listen(http_server_fd_, 5);
        
        running_.store(true);
        start_time_ = std::chrono::steady_clock::now();
        last_heartbeat_time_ = start_time_;
        accept_thread_ = std::thread(&GUIBroadcaster::accept_loop, this);
        broadcast_thread_ = std::thread(&GUIBroadcaster::broadcast_loop, this);
        receive_thread_ = std::thread(&GUIBroadcaster::receive_loop, this);
        http_thread_ = std::thread(&GUIBroadcaster::http_loop, this);
        printf("[GUI] WebSocket server started on port %d, HTTP on port %d\n", PORT, HTTP_PORT);
        return true;
    }
    
    void stop() {
        if (!running_.load()) return;
        running_.store(false);
        
        // Close server sockets to unblock accept
        if (server_fd_ != SOCKET_ERROR_VAL) { CLOSE_SOCKET(server_fd_); server_fd_ = SOCKET_ERROR_VAL; }
        if (http_server_fd_ != SOCKET_ERROR_VAL) { CLOSE_SOCKET(http_server_fd_); http_server_fd_ = SOCKET_ERROR_VAL; }
        
        if (accept_thread_.joinable()) accept_thread_.join();
        if (broadcast_thread_.joinable()) broadcast_thread_.join();
        if (receive_thread_.joinable()) receive_thread_.join();
        if (http_thread_.joinable()) http_thread_.join();
        
        { std::lock_guard<std::mutex> lock(clients_mutex_); for (auto& fd : clients_) CLOSE_SOCKET(fd); clients_.clear(); }
        printf("[GUI] Server stopped\n");
    }
    
    size_t clientCount() const { std::lock_guard<std::mutex> lock(clients_mutex_); return clients_.size(); }
    
    // =============================================================================
    // STATE UPDATE METHODS - Called from main_dual.cpp
    // =============================================================================
    
    void updateTick(const char* sym, double bid, double ask, double ofi, double vpin, double pressure, double spread) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        strncpy(state_.symbol, sym, 15); state_.symbol[15] = '\0';
        state_.bid = bid; state_.ask = ask; state_.mid = (bid + ask) / 2.0;
        state_.ofi = ofi; state_.vpin = vpin; state_.pressure = pressure; state_.spread = spread;
    }
    void updateFusion(int regime, double confidence) { std::lock_guard<std::mutex> lock(state_mutex_); state_.regime = regime; state_.confidence = confidence; }
    
    // v4.9.27: Fixed signature - main_dual.cpp uses 4 args
    void updateRisk(double pnl, double dd_used, double global_exp, int positions) { 
        std::lock_guard<std::mutex> lock(state_mutex_); 
        state_.pnl = pnl; 
        state_.dd_used = dd_used; 
        state_.global_exposure = global_exp; 
        state_.positions = positions; 
    }
    
    // v4.9.27: Added missing method
    void updateDrawdownUsed(double dd_used) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.dd_used = dd_used;
    }
    
    // v4.9.27: Added missing method
    void updateStateGated(uint64_t gated) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.state_gated = gated;
    }
    
    // v4.9.27: Added missing method
    void initSymbols() {
        // No-op: Symbol initialization handled by per-symbol updates
        // This method exists for interface compatibility
    }
    
    // v4.9.27: Added missing method for trade broadcasts
    void broadcastTrade(const char* symbol, const char* side, double qty, double price, double pnl) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.has_trade = true;
        strncpy(state_.trade_symbol, symbol, 15);
        state_.trade_symbol[15] = '\0';
        strncpy(state_.trade_side, side, 7);
        state_.trade_side[7] = '\0';
        state_.trade_qty = qty;
        state_.trade_price = price;
        state_.trade_pnl = pnl;
    }
    
    // v4.9.27: Added missing method for microstructure updates  
    void updateMicro(double ofi, double vpin, double pressure, double spread, double bid, double ask, const char* symbol) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.ofi = ofi;
        state_.vpin = vpin;
        state_.pressure = pressure;
        state_.spread = spread;
        state_.bid = bid;
        state_.ask = ask;
        state_.mid = (bid + ask) / 2.0;
        strncpy(state_.symbol, symbol, 15);
        state_.symbol[15] = '\0';
    }
    
    void updateOrderflow(uint64_t ticks, uint64_t sent, uint64_t filled, uint64_t rejects, uint64_t lat_ns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.ticks_processed = ticks; state_.orders_sent = sent; state_.orders_filled = filled; state_.orders_rejected = rejects;
        (void)lat_ns;  // Reserved for future use
    }
    void updateLatencyStats(uint64_t avg_ns, uint64_t min_ns, uint64_t max_ns, uint64_t p50_ns, uint64_t p99_ns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.avg_latency_ns = avg_ns; state_.min_latency_ns = min_ns; state_.max_latency_ns = max_ns;
        state_.p50_latency_ns = p50_ns; state_.p99_latency_ns = p99_ns;
    }
    
    // v4.9.10: Hot-path order latency (send → ACK) - the REAL latency that matters
    // This is called from main_dual.cpp with data from HotPathLatencyTracker
    void updateHotPathLatency(double min_ms, double p10_ms, double p50_ms, double p90_ms, double p99_ms,
                              uint64_t samples, uint64_t spikes_filtered, const char* state, const char* exec_mode) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.hot_path_min_ms = min_ms;
        state_.hot_path_p10_ms = p10_ms;
        state_.hot_path_p50_ms = p50_ms;
        state_.hot_path_p90_ms = p90_ms;
        state_.hot_path_p99_ms = p99_ms;
        state_.hot_path_samples = samples;
        state_.hot_path_spikes = spikes_filtered;
        strncpy(state_.hot_path_state, state, 15);
        state_.hot_path_state[15] = '\0';
        strncpy(state_.hot_path_exec_mode, exec_mode, 15);
        state_.hot_path_exec_mode[15] = '\0';
    }
    
    // v4.9.34: CFD FIX latency (CO-LOCATED EDGE)
    void updateCfdLatency(double min_ms, double avg_ms, double max_ms, double p50_ms, double p99_ms,
                          uint64_t samples, const char* state) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.cfd_lat_min_ms = min_ms;
        state_.cfd_lat_avg_ms = avg_ms;
        state_.cfd_lat_max_ms = max_ms;
        state_.cfd_lat_p50_ms = p50_ms;
        state_.cfd_lat_p99_ms = p99_ms;
        state_.cfd_lat_samples = samples;
        strncpy(state_.cfd_lat_state, state, 15);
        state_.cfd_lat_state[15] = '\0';
    }
    
    // v4.9.10: System mode and probe stats for bootstrap
    void updateSystemMode(const char* mode, uint32_t probes_sent, uint32_t probes_acked) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        strncpy(state_.system_mode, mode, 15);
        state_.system_mode[15] = '\0';
        state_.probes_sent = probes_sent;
        state_.probes_acked = probes_acked;
    }
    
    // v4.9.25: Venue/execution state for visibility into FROZEN state
    // v4.9.27: Added signature_rejections parameter
    void updateVenueState(const char* venue_state, bool execution_frozen, 
                          const char* frozen_symbols, uint32_t consecutive_failures,
                          uint64_t signature_rejections = 0) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        strncpy(state_.venue_state, venue_state, 23);
        state_.venue_state[23] = '\0';
        state_.execution_frozen = execution_frozen;
        strncpy(state_.frozen_symbols, frozen_symbols ? frozen_symbols : "", 63);
        state_.frozen_symbols[63] = '\0';
        state_.consecutive_failures = consecutive_failures;
        state_.signature_rejections = signature_rejections;
    }
    
    void updatePipelineLatency(uint64_t tick_to_signal_ns, uint64_t signal_to_order_ns, uint64_t order_to_ack_ns) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.tick_to_signal_ns = tick_to_signal_ns; state_.signal_to_order_ns = signal_to_order_ns;
        state_.order_to_ack_ns = order_to_ack_ns; state_.total_latency_ns = tick_to_signal_ns + signal_to_order_ns + order_to_ack_ns;
    }
    
    void updateConnections(bool ctrader, uint32_t fix_reconnects = 0) { 
        std::lock_guard<std::mutex> lock(state_mutex_); 
        state_.ctrader_connected = ctrader; 
        state_.fix_reconnects = fix_reconnects;
    }
    void updateHeartbeat(uint64_t hb, double loop_ms, double drift_ms) { std::lock_guard<std::mutex> lock(state_mutex_); state_.heartbeat = hb; state_.loop_ms = loop_ms; state_.drift_ms = drift_ms; }
    
    // v4.6.0: ML Feature Logger stats
    void updateMLStats(uint64_t features, uint64_t trades, uint64_t written, uint64_t dropped) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.ml_features_logged = features;
        state_.ml_trades_logged = trades;
        state_.ml_records_written = written;
        state_.ml_records_dropped = dropped;
    }
    
    // v4.6.0: Full ML execution stats
    void updateMLExecutionStats(uint64_t gate_accepts, uint64_t gate_rejects, double accept_rate,
                                 double rolling_q50, double rolling_q10, bool drift_kill, bool drift_throttle,
                                 uint64_t venue_fix, uint64_t venue_cfd) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.ml_gate_accepts = gate_accepts;
        state_.ml_gate_rejects = gate_rejects;
        state_.ml_gate_accept_rate = accept_rate;
        state_.ml_rolling_q50 = rolling_q50;
        state_.ml_rolling_q10 = rolling_q10;
        state_.ml_drift_kill = drift_kill;
        state_.ml_drift_throttle = drift_throttle;
        state_.ml_venue_fix = venue_fix;
        state_.ml_venue_cfd = venue_cfd;
    }
    
    void updateExecution(int throttle, double slippage) { std::lock_guard<std::mutex> lock(state_mutex_); state_.throttle_level = throttle; state_.slippage_bps = slippage; }
    void updateSystem(double cpu, double mem, uint64_t uptime) { std::lock_guard<std::mutex> lock(state_mutex_); state_.cpu_pct = cpu; state_.mem_pct = mem; state_.uptime_sec = uptime; }
    void updateQualityFactors(double Qvol, double Qspr, double Qliq, double Qlat, double Qdd, double corr_p) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.Q_vol = Qvol; state_.Q_spr = Qspr; state_.Q_liq = Qliq; state_.Q_lat = Qlat; state_.Q_dd = Qdd; state_.corr_penalty = corr_p;
        state_.risk_multiplier = Qvol * Qspr * Qliq * Qlat * Qdd * corr_p;
    }
    void updateRegime(double vol_z, double spr_z, double liq_z, double lat_z, bool trending, bool vol, int utc_hour) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.vol_z = vol_z; state_.spread_z = spr_z; state_.liq_z = liq_z; state_.lat_z = lat_z;
        state_.is_trending = trending; state_.is_volatile = vol; state_.utc_hour = utc_hour;
    }
    void updateBuckets(int buy_v, int sell_v, int8_t consensus, bool vetoed, const char* veto_reason) { 
        std::lock_guard<std::mutex> lock(state_mutex_); 
        state_.buy_votes = buy_v; 
        state_.sell_votes = sell_v; 
        state_.consensus = consensus; 
        state_.vetoed = vetoed;
        if (veto_reason) { strncpy(state_.veto_reason, veto_reason, 31); state_.veto_reason[31] = '\0'; }
    }
    void updateVeto(bool vetoed, const char* reason) { std::lock_guard<std::mutex> lock(state_mutex_); state_.vetoed = vetoed; if (reason) { strncpy(state_.veto_reason, reason, 31); state_.veto_reason[31] = '\0'; } }
    void updateMarketState(MarketState ms, TradeIntent ti, int conviction, const char* reason) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.market_state = ms; state_.trade_intent = ti; state_.conviction_score = conviction;
        if (reason) { strncpy(state_.state_reason, reason, 31); state_.state_reason[31] = '\0'; }
    }
    void addDiagnostic(const char* msg) { std::lock_guard<std::mutex> lock(state_mutex_); state_.addDiagMsg(msg); }
    void updateStrategyWeights(const double* w, int n) { std::lock_guard<std::mutex> lock(state_mutex_); state_.num_strategies = n; for (int i = 0; i < n && i < 32; i++) state_.weights[i] = w[i]; }
    
    // v4.9.8: Governor Heat telemetry
    void updateGovernorHeat(const char* symbol, double heat, double size_mult, const char* state) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        GUIState::GovernorHeatData* data = nullptr;
        if (strcmp(symbol, "BTCUSDT") == 0 || strcmp(symbol, "BTC") == 0) data = &state_.gov_heat_btc;
        else if (strcmp(symbol, "ETHUSDT") == 0 || strcmp(symbol, "ETH") == 0) data = &state_.gov_heat_eth;
        else if (strcmp(symbol, "SOLUSDT") == 0 || strcmp(symbol, "SOL") == 0) data = &state_.gov_heat_sol;
        if (data) {
            data->heat = heat;
            data->size_mult = size_mult;
            strncpy(data->state, state, 15);
            data->state[15] = '\0';
        }
    }
    
    // v7.12: Per-symbol expectancy data
    void updateExpectancy(const char* symbol, double exp_bps, int trades, double win_rate, double flip_rate, double avg_hold_ms, bool disabled, const char* reason) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // Find or add symbol
        int idx = -1;
        for (int i = 0; i < state_.expectancy_count; i++) {
            if (strcmp(state_.expectancy[i].symbol, symbol) == 0) { idx = i; break; }
        }
        if (idx < 0 && state_.expectancy_count < GUIState::MAX_EXPECTANCY_SYMBOLS) {
            idx = state_.expectancy_count++;
            strncpy(state_.expectancy[idx].symbol, symbol, 15);
            state_.expectancy[idx].symbol[15] = '\0';
        }
        if (idx >= 0) {
            state_.expectancy[idx].expectancy_bps = exp_bps;
            state_.expectancy[idx].trades = trades;
            state_.expectancy[idx].win_rate = win_rate;
            state_.expectancy[idx].flip_rate = flip_rate;
            state_.expectancy[idx].avg_hold_ms = avg_hold_ms;
            state_.expectancy[idx].disabled = disabled;
            if (reason) { strncpy(state_.expectancy[idx].disable_reason, reason, 31); state_.expectancy[idx].disable_reason[31] = '\0'; }
        }
    }
    
    // v3.0: Expectancy Health Panel data
    void updateExpectancyHealth(const char* symbol, const char* regime, double exp_bps, double slope, 
                                 double slope_delta, double div_bps, int div_streak,
                                 const char* session, const char* state_str, const char* pause_reason) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // Find or add symbol
        int idx = -1;
        for (int i = 0; i < state_.health_count; i++) {
            if (strcmp(state_.health[i].symbol, symbol) == 0) { idx = i; break; }
        }
        if (idx < 0 && state_.health_count < GUIState::MAX_EXPECTANCY_SYMBOLS) {
            idx = state_.health_count++;
        }
        if (idx >= 0) {
            strncpy(state_.health[idx].symbol, symbol, 15);
            state_.health[idx].symbol[15] = '\0';
            strncpy(state_.health[idx].regime, regime ? regime : "UNKNOWN", 15);
            state_.health[idx].regime[15] = '\0';
            state_.health[idx].expectancy_bps = exp_bps;
            state_.health[idx].slope = slope;
            state_.health[idx].slope_delta = slope_delta;
            state_.health[idx].divergence_bps = div_bps;
            state_.health[idx].divergence_streak = div_streak;
            strncpy(state_.health[idx].session, session ? session : "N", 7);
            state_.health[idx].session[7] = '\0';
            strncpy(state_.health[idx].state, state_str ? state_str : "OFF", 7);
            state_.health[idx].state[7] = '\0';
            strncpy(state_.health[idx].pause_reason, pause_reason ? pause_reason : "", 23);
            state_.health[idx].pause_reason[23] = '\0';
        }
    }
    
    // v4.9.1: Trade event (goes to blotter)
    void recordTrade(const char* symbol, const char* side, double qty, double price, double pnl,
                     uint8_t engine = 255, uint8_t strategy = 255) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.has_trade = true;
        strncpy(state_.trade_symbol, symbol, 15); state_.trade_symbol[15] = '\0';
        strncpy(state_.trade_side, side, 7); state_.trade_side[7] = '\0';
        state_.trade_qty = qty;
        state_.trade_price = price;
        state_.trade_pnl = pnl;
        state_.trade_engine = engine;
        state_.trade_strategy = strategy;
    }
    
    // v4.9.1: Connection alert
    void setConnectionAlert(bool active, const char* msg = nullptr) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.connection_alert = active;
        if (msg) {
            strncpy(state_.connection_alert_msg, msg, 63);
            state_.connection_alert_msg[63] = '\0';
        }
        if (active) {
            state_.last_connection_alert_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }
    
    // Update symbols for symbol ticker display
    void updateSymbolTick(const char* symbol, double bid, double ask, double network_latency = 0.2) {
        std::lock_guard<std::mutex> lock(symbols_mutex_);
        
        // DEBUG: Log every call
        static int call_count = 0;
        if (++call_count <= 5) {
            printf("[GUI-DEBUG] updateSymbolTick(%s, bid=%.2f, ask=%.2f) call #%d\n", 
                   symbol, bid, ask, call_count);
        }
        
        // Find or add
        for (size_t i = 0; i < symbol_count_; i++) {
            if (strcmp(symbols_[i].symbol, symbol) == 0) {
                symbols_[i].update(bid, ask, network_latency);
                return;
            }
        }
        if (symbol_count_ < MAX_SYMBOLS) {
            strncpy(symbols_[symbol_count_].symbol, symbol, 15);
            symbols_[symbol_count_].symbol[15] = '\0';
            // Determine asset class from symbol
            // v4.12.0: Crypto removed - classify CFD assets
            if (strstr(symbol, "XAU") || strstr(symbol, "XAG")) {
                symbols_[symbol_count_].asset_class = 2;  // Metals
            } else if (strstr(symbol, "NAS") || strstr(symbol, "SPX") || strstr(symbol, "US30") || strstr(symbol, "UK100") || strstr(symbol, "GER40")) {
                symbols_[symbol_count_].asset_class = 3;  // Indices
            } else if (strstr(symbol, "USD") || strstr(symbol, "JPY") || strstr(symbol, "EUR") || strstr(symbol, "GBP") || strstr(symbol, "CHF") || strstr(symbol, "AUD") || strstr(symbol, "CAD") || strstr(symbol, "NZD")) {
                symbols_[symbol_count_].asset_class = 1;  // Forex
            } else {
                symbols_[symbol_count_].asset_class = 0;  // Other CFD
            }
            symbols_[symbol_count_].update(bid, ask, network_latency);
            symbol_count_++;
        }
    }
    
private:
    static const char* marketStateStr(MarketState s) {
        switch (s) {
            case MarketState::DEAD: return "DEAD";
            case MarketState::TRENDING: return "TRENDING";
            case MarketState::RANGING: return "RANGING";
            case MarketState::VOLATILE: return "VOLATILE";
            default: return "UNKNOWN";
        }
    }
    
    static const char* tradeIntentStr(TradeIntent ti) {
        switch (ti) {
            case TradeIntent::NO_TRADE: return "NO_TRADE";
            case TradeIntent::MOMENTUM: return "MOMENTUM";
            case TradeIntent::MEAN_REVERSION: return "MEAN_REVERSION";
            default: return "UNKNOWN";
        }
    }
    
    void accept_loop() {
        while (running_.load()) {
            sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr);
            socket_t client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd == SOCKET_ERROR_VAL) { if (!running_.load()) break; continue; }
            if (handshake(client_fd)) {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                if (clients_.size() < MAX_CLIENTS) { clients_.push_back(client_fd); printf("[GUI] Client connected (%zu total)\n", clients_.size()); }
                else { CLOSE_SOCKET(client_fd); printf("[GUI] Max clients reached\n"); }
            } else { CLOSE_SOCKET(client_fd); }
        }
    }
    
    bool handshake(socket_t fd) {
        char buf[1024] = {0};
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return false;
        std::string request(buf);
        size_t key_pos = request.find("Sec-WebSocket-Key:");
        if (key_pos == std::string::npos) return false;
        size_t key_start = key_pos + 19; while (key_start < request.size() && request[key_start] == ' ') key_start++;
        size_t key_end = request.find("\r\n", key_start); if (key_end == std::string::npos) return false;
        std::string client_key = request.substr(key_start, key_end - key_start);
        std::string accept_key = ws::compute_accept_key(client_key);
        std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_key + "\r\n\r\n";
        send(fd, response.c_str(), response.size(), 0);
        return true;
    }
    
    void receive_loop() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::vector<socket_t> to_check;
            { std::lock_guard<std::mutex> lock(clients_mutex_); to_check = clients_; }
            for (auto fd : to_check) {
                uint8_t buf[1024];
#ifdef _WIN32
                u_long available = 0; ioctlsocket(fd, FIONREAD, &available);
#else
                int available = 0; ioctl(fd, FIONREAD, &available);
#endif
                if (available > 0) {
                    int n = recv(fd, reinterpret_cast<char*>(buf), std::min(static_cast<int>(sizeof(buf)), available), 0);
                    if (n > 0) { std::string payload; uint8_t opcode; if (ws::parse_frame(buf, n, payload, opcode)) { handle_message(fd, opcode, payload); } }
                }
            }
        }
    }
    
    void handle_message(socket_t fd, uint8_t opcode, const std::string& payload) {
        if (opcode == 0x08) { std::lock_guard<std::mutex> lock(clients_mutex_); clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end()); CLOSE_SOCKET(fd); return; }
        if (opcode == 0x09) { auto pong = ws::make_pong_frame(std::vector<uint8_t>(payload.begin(), payload.end())); send(fd, reinterpret_cast<const char*>(pong.data()), pong.size(), 0); return; }
        if (opcode == 0x01) {
            std::string type = json::getString(payload, "type");
            if (type == "config") {
                // v4.9.27: TradingConfig is a class with per-symbol configs
                // Direct field access not supported from GUI - use proper API
                printf("[GUI] Config update received (use TradingConfig API for changes)\n");
            } else if (type == "kill" && kill_switch_) {
                printf("[GUI] KILL SWITCH TRIGGERED FROM DASHBOARD!\n");
                kill_switch_->kill();
            } else if (type == "bring_up") {
                std::string action = json::getString(payload, "action");
                std::string symbol = json::getString(payload, "symbol");
                if (action == "retry" && !symbol.empty()) {
                    // v4.9.27: BringUpManager::retrySymbol not implemented - log only
                    printf("[GUI] Bring-up retry requested for %s (not implemented)\n", symbol.c_str());
                }
            } else if (type == "symbol_control") {
                // v4.3.2: Handle symbol enable/disable from GUI
                std::string action = json::getString(payload, "action");
                std::string symbol = json::getString(payload, "symbol");
                if (!symbol.empty()) {
                    if (action == "enable") {
                        SymbolEnabledManager::instance().setEnabled(symbol.c_str(), true);
                        printf("[GUI] Symbol %s ENABLED from dashboard\n", symbol.c_str());
                    } else if (action == "disable") {
                        SymbolEnabledManager::instance().setEnabled(symbol.c_str(), false);
                        printf("[GUI] Symbol %s DISABLED from dashboard\n", symbol.c_str());
                    }
                }
            }
        }
    }
    
    void broadcast_loop() {
        while (running_.load()) {
            auto start = std::chrono::steady_clock::now();
            std::string json = build_state_json();
            auto frame = ws::make_text_frame(json);
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                auto it = clients_.begin();
                while (it != clients_.end()) {
                    socket_t fd = *it;
#ifndef _WIN32
                    struct pollfd pfd = {fd, POLLOUT, 0};
                    int poll_res = poll(&pfd, 1, 10);
                    if (poll_res > 0 && (pfd.revents & POLLOUT)) {
                        ssize_t sent = send(fd, reinterpret_cast<const char*>(frame.data()), frame.size(), MSG_NOSIGNAL);
                        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            printf("[GUI] Send error on fd %d, removing client\n", fd);
                            CLOSE_SOCKET(fd);
                            it = clients_.erase(it);
                            continue;
                        }
                    } else if (poll_res < 0) {
                        printf("[GUI] Poll error on fd %d, removing client\n", fd);
                        CLOSE_SOCKET(fd);
                        it = clients_.erase(it);
                        continue;
                    }
#else
                    send(fd, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()), 0);
#endif
                    ++it;
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
        
        // v7.15: Compute uptime fresh each time (not relying on updateState)
        s.uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count();
        
        double avg_us = s.avgLatencyUs(), min_us = s.minLatencyUs(), max_us = s.maxLatencyUs();
        double p50_us = s.p50LatencyUs(), p99_us = s.p99LatencyUs(), avg_ms = s.avgLatencyMs();
        double tick_to_signal_us = static_cast<double>(s.tick_to_signal_ns) / 1000.0;
        double signal_to_order_us = static_cast<double>(s.signal_to_order_ns) / 1000.0;
        double order_to_ack_us = static_cast<double>(s.order_to_ack_ns) / 1000.0;
        double total_us = static_cast<double>(s.total_latency_ns) / 1000.0;
        
        // v4.9.26: Use REAL hot-path latency for network_latency display (removed fake random values)
        // If hot_path has samples, use p50. Otherwise, show 0.0 to indicate NO DATA.
        double net_lat_current = (s.hot_path_samples > 0) ? s.hot_path_p50_ms : 0.0;
        double net_lat_avg = net_lat_current;
        double net_lat_min = (s.hot_path_samples > 0) ? s.hot_path_min_ms : 0.0;
        double net_lat_max = (s.hot_path_samples > 0) ? s.hot_path_p99_ms : 0.0;
        
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
            "{\"type\":\"snapshot\","
            "\"engine\":{\"heartbeat\":%llu,\"loop_ms\":%.3f,\"drift_ms\":%.3f},"
            "\"micro\":{\"ofi\":%.6f,\"vpin\":%.4f,\"pressure\":%.4f,\"spread\":%.6f,\"tick\":{\"symbol\":\"%s\",\"bid\":%.8f,\"ask\":%.8f,\"mid\":%.8f}},"
            "\"fusion\":{\"regime\":%d,\"confidence\":%.4f},"
            "\"risk\":{\"pnl\":%.4f,\"dd\":%.4f,\"dd_used\":%.4f,\"global\":%.6f,\"positions\":%d},"
            "\"orderflow\":{\"ticks\":%llu,\"orders_sent\":%llu,\"orders_filled\":%llu,\"rejects\":%llu,\"latency_ms\":%.3f,\"exec_latency_ms\":%.3f},"
            "\"connections\":{\"latency\":{\"quote_ms\":%.3f,\"trade_ms\":%.3f,\"avg_ms\":%.3f}},"
            "\"latency\":{\"avg_us\":%.2f,\"min_us\":%.2f,\"max_us\":%.2f,\"p50_us\":%.2f,\"p99_us\":%.2f,\"pipeline\":{\"tick_to_signal_us\":%.2f,\"signal_to_order_us\":%.2f,\"order_to_ack_us\":%.2f,\"total_us\":%.2f}},"
            "\"network_latency\":{\"current_ms\":%.3f,\"avg_ms\":%.3f,\"min_ms\":%.3f,\"max_ms\":%.3f},",
            (unsigned long long)s.heartbeat, s.loop_ms, s.drift_ms, s.ofi, s.vpin, s.pressure, s.spread, s.symbol, s.bid, s.ask, s.mid,
            s.regime, s.confidence, s.pnl, s.drawdown, s.dd_used, s.global_exposure, s.positions,
            (unsigned long long)s.ticks_processed, (unsigned long long)s.orders_sent, (unsigned long long)s.orders_filled, (unsigned long long)s.orders_rejected, avg_ms, execution_latency_ms_,
            s.loop_ms, s.loop_ms * 1.2, s.loop_ms * 0.95,  // connections.latency: USE LOOP_MS (always updates!)
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
            "\"system\":{\"cpu\":%.1f,\"mem\":%.1f,\"uptime\":%llu,\"uptime_str\":\"%s\",\"ctrader\":%s,\"fix_reconnects\":%u,\"version\":\"%s\"}}",
            s.Q_vol, s.Q_spr, s.Q_liq, s.Q_lat, s.Q_dd, s.corr_penalty, s.risk_multiplier,
            s.vol_z, s.spread_z, s.liq_z, s.lat_z, s.is_trending ? "true" : "false", s.is_volatile ? "true" : "false", s.utc_hour, s.vetoed ? "true" : "false", s.veto_reason,
            s.buy_votes, s.sell_votes, (int)s.consensus,
            marketStateStr(s.market_state), tradeIntentStr(s.trade_intent), s.conviction_score, s.state_reason,
            (unsigned long long)s.state_gated,
            s.throttle_level, s.slippage_bps, s.cpu_pct, s.mem_pct, (unsigned long long)s.uptime_sec, formatUptime(s.uptime_sec).c_str(),
            s.ctrader_connected ? "true" : "false", s.fix_reconnects, version_.c_str());
        
        // buf2 ends with "}}" - remove the last "}" so we can add more fields
        std::string buf2_str = buf2;
        if (!buf2_str.empty() && buf2_str.back() == '}') {
            buf2_str.pop_back();
        }
        result += buf2_str;
        
        // Add trade event if present - v6.80: Include PnL, v4.9.1: Include engine/strategy
        if (s.has_trade) {
            char trade_buf[512];
            // Convert engine/strategy IDs to strings
            const char* engine_str = "UNKNOWN";
            const char* strategy_str = "Unknown";
            switch (s.trade_engine) {
                case 0: engine_str = "CFD"; break;
                case 1: engine_str = "INCOME"; break;
            }
            switch (s.trade_strategy) {
                case 10: strategy_str = "PureScalper"; break;
                case 11: strategy_str = "Predator"; break;
                case 12: strategy_str = "OpenRange"; break;
                case 13: strategy_str = "VwapDefense"; break;
                case 14: strategy_str = "StopRunFade"; break;
                case 15: strategy_str = "SessionHandoff"; break;
                case 16: strategy_str = "LiquidityVacuum"; break;
                case 20: strategy_str = "IncomeMeanRev"; break;
            }
            snprintf(trade_buf, sizeof(trade_buf),
                ",\"trade\":{\"symbol\":\"%s\",\"side\":\"%s\",\"qty\":%.6f,\"price\":%.6f,\"pnl\":%.6f,\"engine\":\"%s\",\"strategy\":\"%s\",\"engine_id\":%d,\"strategy_id\":%d}",
                s.trade_symbol, s.trade_side, s.trade_qty, s.trade_price, s.trade_pnl,
                engine_str, strategy_str, (int)s.trade_engine, (int)s.trade_strategy);
            result += trade_buf;
        }
        
        // v4.9.1: Add connection alert if active
        if (s.connection_alert) {
            char alert_buf[256];
            snprintf(alert_buf, sizeof(alert_buf),
                ",\"connection_alert\":{\"active\":true,\"message\":\"%s\",\"time\":%llu}",
                s.connection_alert_msg, (unsigned long long)s.last_connection_alert_time);
            result += alert_buf;
        }
        
        // v4.6.0: Add ML Feature Logger stats
        {
            char ml_buf[256];
            snprintf(ml_buf, sizeof(ml_buf),
                ",\"ml_logger\":{\"features_logged\":%llu,\"trades_logged\":%llu,\"records_written\":%llu,\"records_dropped\":%llu}",
                (unsigned long long)s.ml_features_logged,
                (unsigned long long)s.ml_trades_logged,
                (unsigned long long)s.ml_records_written,
                (unsigned long long)s.ml_records_dropped);
            result += ml_buf;
        }
        
        // v4.6.0: Add ML Gate, Drift Guard, Venue Router stats
        {
            char ml_exec_buf[512];
            snprintf(ml_exec_buf, sizeof(ml_exec_buf),
                ",\"ml_gate\":{\"accepts\":%llu,\"rejects\":%llu,\"accept_rate\":%.2f}"
                ",\"ml_drift\":{\"rolling_q50\":%.4f,\"rolling_q10\":%.4f,\"kill\":%s,\"throttle\":%s}"
                ",\"ml_venue\":{\"fix\":%llu,\"cfd\":%llu}",
                (unsigned long long)s.ml_gate_accepts,
                (unsigned long long)s.ml_gate_rejects,
                s.ml_gate_accept_rate,
                s.ml_rolling_q50, s.ml_rolling_q10,
                s.ml_drift_kill ? "true" : "false",
                s.ml_drift_throttle ? "true" : "false",
                (unsigned long long)s.ml_venue_fix,
                (unsigned long long)s.ml_venue_cfd);
            result += ml_exec_buf;
        }
        
        result += ",\"config\":";
        result += getTradingConfig().toJSON();
        
        // Add bring-up visibility data
        result += ",\"bring_up\":";
        result += getBringUpManager().getDashboardJSON();
        
        // v4.5.1: Add NAS100 ownership data
        {
            auto nas_state = getNAS100OwnershipState();
            char nas_buf[512];
            snprintf(nas_buf, sizeof(nas_buf),
                ",\"nas100_ownership\":{"
                "\"owner\":\"%s\","
                "\"income_window_active\":%s,"
                "\"cfd_no_new_entries\":%s,"
                "\"ny_time\":\"%02d:%02d\","
                "\"seconds_to_income\":%d,"
                "\"seconds_in_income\":%d,"
                "\"cfd_forced_flat_seconds\":%d,"
                "\"income_locked\":%s"
                "}",
                nas100_owner_str(nas_state.current_owner),
                nas_state.income_window_active ? "true" : "false",
                nas_state.cfd_no_new_entries ? "true" : "false",
                nas_state.ny_hour, nas_state.ny_minute,
                nas_state.seconds_to_income_window,
                nas_state.seconds_in_income_window,
                nas_state.cfd_forced_flat_seconds,
                EngineOwnership::instance().isIncomeLocked() ? "true" : "false");
            result += nas_buf;
        }
        
        // v4.5.1: Add Risk Governor data
        {
            char risk_buf[512];
            GlobalRiskGovernor::instance().toJSON(risk_buf, sizeof(risk_buf));
            result += ",\"risk_governor\":";
            result += risk_buf;
        }
        
        // v4.9.8: Add Governor Heat telemetry
        {
            char heat_buf[512];
            snprintf(heat_buf, sizeof(heat_buf),
                ",\"governor_heat\":{"
                "\"btc\":{\"heat\":%.3f,\"size_mult\":%.3f,\"state\":\"%s\"},"
                "\"eth\":{\"heat\":%.3f,\"size_mult\":%.3f,\"state\":\"%s\"},"
                "\"sol\":{\"heat\":%.3f,\"size_mult\":%.3f,\"state\":\"%s\"}"
                "}",
                s.gov_heat_btc.heat, s.gov_heat_btc.size_mult, s.gov_heat_btc.state,
                s.gov_heat_eth.heat, s.gov_heat_eth.size_mult, s.gov_heat_eth.state,
                s.gov_heat_sol.heat, s.gov_heat_sol.size_mult, s.gov_heat_sol.state);
            result += heat_buf;
        }
        
        // v4.9.10: Add Hot-Path Latency (send → ACK) - THE ONLY REAL LATENCY SOURCE
        // This is wired from HotPathLatencyTracker
        {
            char lat_buf[512];
            snprintf(lat_buf, sizeof(lat_buf),
                ",\"hot_path_latency\":{"
                "\"min_ms\":%.3f,"
                "\"p10_ms\":%.3f,"
                "\"p50_ms\":%.3f,"
                "\"p90_ms\":%.3f,"
                "\"p99_ms\":%.3f,"
                "\"samples\":%llu,"
                "\"spikes_filtered\":%llu,"
                "\"state\":\"%s\","
                "\"exec_mode\":\"%s\","
                "\"system_mode\":\"%s\","
                "\"probes_sent\":%u,"
                "\"probes_acked\":%u"
                "}",
                s.hot_path_min_ms, s.hot_path_p10_ms, s.hot_path_p50_ms,
                s.hot_path_p90_ms, s.hot_path_p99_ms,
                (unsigned long long)s.hot_path_samples,
                (unsigned long long)s.hot_path_spikes,
                s.hot_path_state, s.hot_path_exec_mode,
                s.system_mode,
                s.probes_sent, s.probes_acked);
            result += lat_buf;
        }
        
        // v4.9.34: Add CFD FIX Latency (CO-LOCATED EDGE)
        // This is the REAL co-location latency from London VPS to cTrader
        {
            char cfd_lat_buf[256];
            snprintf(cfd_lat_buf, sizeof(cfd_lat_buf),
                ",\"cfd_latency\":{"
                "\"min_ms\":%.3f,"
                "\"avg_ms\":%.3f,"
                "\"max_ms\":%.3f,"
                "\"p50_ms\":%.3f,"
                "\"p99_ms\":%.3f,"
                "\"samples\":%llu,"
                "\"state\":\"%s\""
                "}",
                s.cfd_lat_min_ms, s.cfd_lat_avg_ms, s.cfd_lat_max_ms,
                s.cfd_lat_p50_ms, s.cfd_lat_p99_ms,
                (unsigned long long)s.cfd_lat_samples,
                s.cfd_lat_state);
            result += cfd_lat_buf;
        }
        
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
        
        // v7.12: Add expectancy panel data
        result += ",\"expectancy\":{";
        for (int i = 0; i < s.expectancy_count; i++) {
            const SymbolExpectancy& e = s.expectancy[i];
            if (i > 0) result += ",";
            char exp_buf[256];
            snprintf(exp_buf, sizeof(exp_buf),
                "\"%s\":{\"E_bps\":%.2f,\"trades\":%d,\"win_rate\":%.3f,\"flip_rate\":%.3f,\"avg_hold_ms\":%.0f,\"disabled\":%s,\"reason\":\"%s\"}",
                e.symbol, e.expectancy_bps, e.trades, e.win_rate, e.flip_rate, e.avg_hold_ms,
                e.disabled ? "true" : "false", e.disable_reason);
            result += exp_buf;
        }
        result += "}";
        
        // v3.0: Add expectancy_health array for the panel
        result += ",\"expectancy_health\":[";
        for (int i = 0; i < s.health_count; i++) {
            const ExpectancyHealthRow& h = s.health[i];
            if (i > 0) result += ",";
            char health_buf[512];
            snprintf(health_buf, sizeof(health_buf),
                "{\"symbol\":\"%s\",\"regime\":\"%s\",\"expectancy_bps\":%.4f,\"slope\":%.6f,"
                "\"slope_delta\":%.6f,\"divergence_bps\":%.4f,\"divergence_streak\":%d,"
                "\"session\":\"%s\",\"state\":\"%s\",\"pause_reason\":\"%s\"}",
                h.symbol, h.regime, h.expectancy_bps, h.slope, h.slope_delta,
                h.divergence_bps, h.divergence_streak, h.session, h.state, h.pause_reason);
            result += health_buf;
        }
        result += "]";
        
        // =========================================================================
        // v4.9.12: REGIME × ALPHA × BROKER DATA
        // =========================================================================
        
        // Regime-Alpha cells
        if (s.regime_alpha_count > 0) {
            result += ",\"regime_alpha_cells\":[";
            for (int i = 0; i < s.regime_alpha_count; i++) {
                const auto& cell = s.regime_alpha_cells[i];
                if (i > 0) result += ",";
                
                char cell_buf[1024];
                snprintf(cell_buf, sizeof(cell_buf),
                    "{"
                    "\"broker\":\"%s\","
                    "\"regime\":\"%s\","
                    "\"alpha\":\"%s\","
                    "\"net_r\":%.3f,"
                    "\"trades\":%d,"
                    "\"win_rate\":%.3f,"
                    "\"sharpe\":%.2f,"
                    "\"fill_rate\":%.3f,"
                    "\"reject_rate\":%.3f,"
                    "\"avg_latency_ms\":%.1f,"
                    "\"slippage_bps\":%.2f,"
                    "\"gross_edge_bps\":%.2f,"
                    "\"spread_paid_bps\":%.2f,"
                    "\"latency_cost_bps\":%.2f,"
                    "\"status\":\"%s\","
                    "\"hourly_exp\":[",
                    cell.broker, cell.regime, cell.alpha,
                    cell.net_r, cell.trades, cell.win_rate, cell.sharpe,
                    cell.fill_rate, cell.reject_rate, cell.avg_latency_ms,
                    cell.slippage_bps, cell.gross_edge_bps, cell.spread_paid_bps,
                    cell.latency_cost_bps, cell.status);
                result += cell_buf;
                
                // Add hourly expectancy array
                for (int h = 0; h < 24; h++) {
                    if (h > 0) result += ",";
                    char hour_buf[32];
                    snprintf(hour_buf, sizeof(hour_buf), "%.3f", cell.hourly_exp[h]);
                    result += hour_buf;
                }
                result += "],\"hourly_trades\":[";
                for (int h = 0; h < 24; h++) {
                    if (h > 0) result += ",";
                    char hour_buf[16];
                    snprintf(hour_buf, sizeof(hour_buf), "%d", cell.hourly_trades[h]);
                    result += hour_buf;
                }
                result += "]}";
            }
            result += "]";
        }
        
        // Retirement events
        if (s.retirement_event_count > 0) {
            result += ",\"retirement_events\":[";
            for (int i = 0; i < s.retirement_event_count; i++) {
                const auto& evt = s.retirement_events[i];
                if (evt.alpha[0] == '\0') continue;
                if (i > 0) result += ",";
                
                char evt_buf[256];
                snprintf(evt_buf, sizeof(evt_buf),
                    "{\"alpha\":\"%s\",\"regime\":\"%s\",\"broker\":\"%s\",\"reason\":\"%s\",\"ts\":%llu}",
                    evt.alpha, evt.regime, evt.broker, evt.reason, (unsigned long long)evt.timestamp_ms);
                result += evt_buf;
            }
            result += "]";
        }
        
        // No-trade reason aggregates
        if (s.no_trade_reason_count > 0) {
            result += ",\"no_trade_reasons\":[";
            for (int i = 0; i < s.no_trade_reason_count; i++) {
                const auto& r = s.no_trade_reasons[i];
                if (r.reason[0] == '\0') continue;
                if (i > 0) result += ",";
                
                char reason_buf[128];
                snprintf(reason_buf, sizeof(reason_buf),
                    "{\"reason\":\"%s\",\"count\":%d,\"pct\":%.1f}",
                    r.reason, r.count, r.pct);
                result += reason_buf;
            }
            result += "]";
        }
        
        // Physics state
        {
            char physics_buf[64];
            snprintf(physics_buf, sizeof(physics_buf), ",\"physics_state\":\"%s\"", s.physics_state);
            result += physics_buf;
        }
        
        // v4.9.25: Venue/Execution state for FROZEN visibility
        {
            char venue_buf[256];
            snprintf(venue_buf, sizeof(venue_buf), 
                ",\"execution_governor\":{"
                "\"venue_state\":\"%s\","
                "\"execution_frozen\":%s,"
                "\"frozen_symbols\":\"%s\","
                "\"consecutive_failures\":%u,"
                "\"signature_rejections\":%llu"
                "}",
                s.venue_state,
                s.execution_frozen ? "true" : "false",
                s.frozen_symbols,
                s.consecutive_failures,
                static_cast<unsigned long long>(s.signature_rejections));
            result += venue_buf;
        }
        
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
            
            // Read HTTP request
            char buffer[4096];
            int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                
                // Check if it's a GET request
                if (strstr(buffer, "GET") != nullptr) {
                    if (strstr(buffer, "chimera_logo.png") != nullptr) {
                        serve_logo(client_fd);
                    } else if (strstr(buffer, "regime_dashboard") != nullptr) {
                        serve_file(client_fd, "regime_dashboard.html");
                    } else {
                        serve_file(client_fd, "chimera_dashboard.html");
                    }
                }
            }
            CLOSE_SOCKET(client_fd);
        }
    }
    
    void serve_logo(socket_t client_fd) {
        // Try to load logo from file
        std::vector<char> png_content;
        FILE* f = fopen("chimera_logo.png", "rb");
        if (!f) f = fopen("../chimera_logo.png", "rb");
        if (!f) f = fopen("/home/trader/chimera_src/chimera_logo.png", "rb");
        if (!f) f = fopen("/home/trader/Chimera/chimera_logo.png", "rb");
        
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            png_content.resize(size);
            size_t bytes_read = fread(png_content.data(), 1, size, f);
            png_content.resize(bytes_read);
            fclose(f);
            
            char header[512];
            snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/png\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "Cache-Control: max-age=86400\r\n"
                "\r\n", png_content.size());
            
            send(client_fd, header, strlen(header), 0);
            send(client_fd, png_content.data(), png_content.size(), 0);
        } else {
            // 404
            const char* response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, response, strlen(response), 0);
        }
    }
    
    // v4.9.12: Generic file server for multiple dashboards
    void serve_file(socket_t client_fd, const char* filename) {
        // Try to load file from multiple paths
        std::string html_content;
        char path[256];
        FILE* f = nullptr;
        
        // Try current directory
        snprintf(path, sizeof(path), "%s", filename);
        f = fopen(path, "r");
        
        // Try parent directory
        if (!f) {
            snprintf(path, sizeof(path), "../%s", filename);
            f = fopen(path, "r");
        }
        
        // Try standard paths
        if (!f) {
            snprintf(path, sizeof(path), "/home/trader/Chimera/%s", filename);
            f = fopen(path, "r");
        }
        if (!f) {
            snprintf(path, sizeof(path), "/root/Chimera/%s", filename);
            f = fopen(path, "r");
        }
        
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
            char error_html[512];
            snprintf(error_html, sizeof(error_html), R"(<!DOCTYPE html>
<html><head><title>Chimera Dashboard</title></head>
<body style="background:#111;color:#f00;font-family:monospace;padding:20px;">
<h1>File Not Found</h1>
<p>ERROR: Could not load %s</p>
<p>Make sure the file exists in the working directory</p>
<p>WebSocket: port 7777 | HTTP: port 8080</p>
<p><a href="/" style="color:#0f0">Main Dashboard</a> | <a href="/regime_dashboard.html" style="color:#0f0">Regime Dashboard</a></p>
</body></html>)", filename);
            html_content = error_html;
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
    LatencyTracker latency_tracker_;  // General GUI latency tracker (separate from HotPathLatencyTracker)
    GlobalKill* kill_switch_;
    std::string version_;
    double execution_latency_ms_ = 0.0;  // v4.31.0: ExecutionMetrics latency bridge
    
    // v4.9.26: Latency tracking unified
    // hot_path_latency is the canonical source
    // network_latency in JSON mirrors hot_path for backward compatibility
};

} // namespace Chimera
