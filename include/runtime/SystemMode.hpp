// ═══════════════════════════════════════════════════════════════════════════════
// include/runtime/SystemMode.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.10: INSTITUTIONAL BOOTSTRAP SYSTEM
// 
// SystemMode tracks whether Chimera has sufficient latency data to trade.
// During BOOTSTRAP: Send probe orders, measure ACK latency, no real trades.
// During LIVE: Normal trading with latency-informed gates.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// System Mode Enum
// ─────────────────────────────────────────────────────────────────────────────
enum class SystemMode : uint8_t {
    BOOTSTRAP = 0,  // Warmup: sending probes, measuring latency
    LIVE      = 1   // Active: trading with real latency data
};

inline const char* systemModeStr(SystemMode m) {
    switch (m) {
        case SystemMode::BOOTSTRAP: return "BOOTSTRAP";
        case SystemMode::LIVE:      return "LIVE";
        default:                    return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Bootstrap State
// v4.9.12: Added backoff tracking for WebSocket disconnects
// v4.9.27: Added ACK-gating - only ONE probe in flight at a time
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolBootstrapState {
    SystemMode mode = SystemMode::BOOTSTRAP;
    uint32_t probes_sent = 0;
    uint32_t probes_acked = 0;
    uint32_t probes_cancelled = 0;
    uint64_t last_probe_ns = 0;
    bool ready = false;
    
    // v4.9.12: Backoff state for WebSocket disconnects
    uint32_t consecutive_send_fails = 0;  // Consecutive failed sends
    uint64_t backoff_until_ns = 0;        // Don't send until this time
    
    // v4.9.27: ACK-GATING - Critical fix for WS stability
    // Only ONE probe in flight per symbol. Wait for ACK or timeout.
    bool probe_in_flight = false;         // Is there a probe waiting for ACK?
    uint64_t in_flight_sent_ns = 0;       // When was it sent?
    uint64_t in_flight_client_id = 0;     // Which probe is in flight?
    
    static constexpr uint64_t PROBE_TIMEOUT_NS = 3'000'000'000ULL;  // 3 second timeout
    
    void reset() {
        mode = SystemMode::BOOTSTRAP;
        probes_sent = 0;
        probes_acked = 0;
        probes_cancelled = 0;
        last_probe_ns = 0;
        ready = false;
        consecutive_send_fails = 0;
        backoff_until_ns = 0;
        probe_in_flight = false;
        in_flight_sent_ns = 0;
        in_flight_client_id = 0;
    }
    
    // v4.9.27: Check if probe timed out
    bool probeTimedOut(uint64_t now_ns) const {
        if (!probe_in_flight) return false;
        return (now_ns - in_flight_sent_ns) >= PROBE_TIMEOUT_NS;
    }
    
    // v4.9.27: Mark probe as in flight
    void markProbeInFlight(uint64_t client_id, uint64_t now_ns) {
        probe_in_flight = true;
        in_flight_sent_ns = now_ns;
        in_flight_client_id = client_id;
    }
    
    // v4.9.27: Clear in-flight status (on ACK, reject, or timeout)
    void clearInFlight() {
        probe_in_flight = false;
        in_flight_sent_ns = 0;
        in_flight_client_id = 0;
    }
    
    // Calculate next backoff duration (exponential: 1s, 2s, 5s, 10s max)
    uint64_t calculateBackoffNs() const {
        constexpr uint64_t SECOND_NS = 1'000'000'000ULL;
        switch (consecutive_send_fails) {
            case 0:
            case 1:  return 1 * SECOND_NS;   // 1 second
            case 2:  return 2 * SECOND_NS;   // 2 seconds
            case 3:  return 5 * SECOND_NS;   // 5 seconds
            default: return 10 * SECOND_NS;  // 10 seconds max
        }
    }
    
    // Check if still in backoff period
    bool inBackoff(uint64_t now_ns) const {
        return now_ns < backoff_until_ns;
    }
    
    // Called on send failure - increases backoff
    void recordSendFail(uint64_t now_ns) {
        consecutive_send_fails++;
        backoff_until_ns = now_ns + calculateBackoffNs();
    }
    
    // Called on successful send - resets backoff
    void recordSendSuccess() {
        consecutive_send_fails = 0;
        backoff_until_ns = 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Probe Configuration Per Symbol
// ─────────────────────────────────────────────────────────────────────────────
struct ProbeConfig {
    uint32_t target_probes;      // How many probes before LIVE
    uint32_t min_confidence;     // Minimum ACKs needed
    double price_offset_pct;     // Distance from mid (e.g., 0.10 = 10%)
    uint32_t spacing_ms;         // Minimum ms between probes
    double probe_qty;            // Quantity for probe orders
};

// Default probe configurations per symbol class
inline ProbeConfig getProbeConfig(const char* symbol) {
    // v4.9.27: INCREASED SPACING to avoid rate limits
    // Rate limit: 1200 requests/10min = 2/sec max
    // Using 1000ms spacing = 1/sec per symbol (safe margin)
    //
    // v4.9.27: REDUCED target_probes from 30 to 5 for faster bootstrap
    // Can increase once latency measurement is confirmed working
    //
    // NOTE: Using GTC (not IOC) because probe is 10% below market
    // IOC would reject immediately since price can't match any sells
    
    // High latency variance configs
    if (strstr(symbol, "BTC") || strstr(symbol, "btc")) {
        return { 5, 3, 0.10, 2000, 0.0001 };  // 10% away, 2s spacing, 5 probes
    }
    if (strstr(symbol, "ETH") || strstr(symbol, "eth")) {
        return { 5, 3, 0.08, 2000, 0.001 };
    }
    if (strstr(symbol, "SOL") || strstr(symbol, "sol")) {
        return { 5, 3, 0.07, 2000, 0.01 };
    }
    
    // METALS - Bridge hop adds jitter
    if (strstr(symbol, "XAU") || strstr(symbol, "GOLD")) {
        return { 5, 3, 0.05, 2500, 0.01 };
    }
    if (strstr(symbol, "XAG") || strstr(symbol, "SILVER")) {
        return { 5, 3, 0.06, 2500, 0.1 };
    }
    
    // INDICES - Generally stable
    if (strstr(symbol, "NAS") || strstr(symbol, "US30") || strstr(symbol, "SPX")) {
        return { 5, 3, 0.02, 3000, 0.01 };
    }
    
    // Default (unknown symbol)
    return { 5, 3, 0.05, 2500, 0.001 };
}

// ─────────────────────────────────────────────────────────────────────────────
// Global System Mode Manager (thread-safe)
// ─────────────────────────────────────────────────────────────────────────────
class SystemModeManager {
public:
    static SystemModeManager& instance() {
        static SystemModeManager inst;
        return inst;
    }
    
    // Global mode (all symbols)
    SystemMode globalMode() const {
        return global_mode_.load(std::memory_order_acquire);
    }
    
    void setGlobalMode(SystemMode m) {
        global_mode_.store(m, std::memory_order_release);
        printf("[SYSTEM_MODE] Global mode changed to: %s\n", systemModeStr(m));
    }
    
    // Check if system is ready for live trading
    bool isLive() const {
        return global_mode_.load(std::memory_order_acquire) == SystemMode::LIVE;
    }
    
    bool isBootstrap() const {
        return global_mode_.load(std::memory_order_acquire) == SystemMode::BOOTSTRAP;
    }
    
private:
    SystemModeManager() : global_mode_(SystemMode::BOOTSTRAP) {}
    std::atomic<SystemMode> global_mode_;
};

// Convenience accessor
inline SystemModeManager& getSystemMode() {
    return SystemModeManager::instance();
}

} // namespace Chimera
