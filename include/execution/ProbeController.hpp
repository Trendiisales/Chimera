// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ProbeController.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.11.0: SINGLE-OWNER PROBE CONTROLLER (CFD only)
//
// DESIGN RULES (non-negotiable):
// - One class owns ALL probe sending
// - Per-symbol state machine
// - ACK-gated (structural, not advisory)
// - IOC only
// - Cancel before retry
// - No parallel probes
// - No burst
// - NO DEPENDENCY ON SymbolConfig - probes are telemetry, not trades
//
// USAGE:
//   auto& probe_ctrl = Chimera::getProbeController();
//   probe_ctrl.registerSymbol("XAUUSD");
//   probe_ctrl.enable();  // Only after engine is RUNNING
//   probe_ctrl.onTick("XAUUSD", mid_price);
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <atomic>
#include <time.h>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Probe State Machine (per symbol)
// ─────────────────────────────────────────────────────────────────────────────
enum class ProbeState : uint8_t {
    IDLE,           // Ready to send
    SENT,           // Waiting for ACK
    CANCELLING,     // Timeout - sent cancel, waiting for cancel ACK
    COOLDOWN        // Brief pause after completion
};

inline const char* probeStateStr(ProbeState s) {
    switch (s) {
        case ProbeState::IDLE:       return "IDLE";
        case ProbeState::SENT:       return "SENT";
        case ProbeState::CANCELLING: return "CANCELLING";
        case ProbeState::COOLDOWN:   return "COOLDOWN";
        default:                     return "?";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Probe Tracking
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolProbeState {
    ProbeState state = ProbeState::IDLE;
    uint64_t client_order_id = 0;      // PRB client ID
    uint64_t exchange_order_id = 0;    // From ACK (for cancel)
    uint64_t sent_ts_ns = 0;           // Monotonic send time
    uint64_t state_enter_ts_ns = 0;    // When entered current state
    uint32_t acks_received = 0;        // Successful probes
    uint32_t rejects_received = 0;     // Failed probes
    uint32_t timeouts = 0;             // Timed out probes
    
    void reset() {
        state = ProbeState::IDLE;
        client_order_id = 0;
        exchange_order_id = 0;
        sent_ts_ns = 0;
        state_enter_ts_ns = 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Callback Types
// ─────────────────────────────────────────────────────────────────────────────
using ProbeSendCallback = std::function<bool(
    const char* symbol,
    double price,
    double qty,
    uint64_t client_order_id,
    bool use_ioc
)>;

using ProbeCancelCallback = std::function<bool(
    const char* symbol,
    uint64_t exchange_order_id
)>;

using ProbeLatencyCallback = std::function<void(
    const char* symbol,
    uint64_t latency_ns
)>;

// ─────────────────────────────────────────────────────────────────────────────
// PROBE CONTROLLER - Single Owner, Single Send Path, NO CONFIG DEPENDENCY
// ─────────────────────────────────────────────────────────────────────────────
class ProbeController {
public:
    // Configuration - all probe-owned, no external config
    static constexpr uint64_t PROBE_INTERVAL_NS = 2'000'000'000ULL;   // 2s between probes
    static constexpr uint64_t PROBE_TIMEOUT_NS = 3'000'000'000ULL;    // 3s timeout
    static constexpr uint64_t COOLDOWN_NS = 500'000'000ULL;           // 500ms after completion
    static constexpr uint64_t CANCEL_TIMEOUT_NS = 2'000'000'000ULL;   // 2s for cancel ACK
    static constexpr double PRICE_OFFSET_PCT = 0.10;                   // 10% below mid
    static constexpr uint32_t TARGET_PROBES = 5;                       // Probes needed per symbol
    static constexpr double PROBE_QTY = 0.0001;                        // Minimal safe qty (CFD)
    
    ProbeController() : enabled_(false), next_client_id_(2'000'000) {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // SETUP
    // ═══════════════════════════════════════════════════════════════════════
    
    void setSendCallback(ProbeSendCallback cb) { send_cb_ = std::move(cb); }
    void setCancelCallback(ProbeCancelCallback cb) { cancel_cb_ = std::move(cb); }
    void setLatencyCallback(ProbeLatencyCallback cb) { latency_cb_ = std::move(cb); }
    
    // v4.9.30: Diagnostic query methods
    bool hasSendCallback() const { return send_cb_ != nullptr; }
    bool hasCancelCallback() const { return cancel_cb_ != nullptr; }
    bool hasLatencyCallback() const { return latency_cb_ != nullptr; }
    
    // Register symbol - NO config needed, probes use fixed minimal qty
    void registerSymbol(const char* symbol) {
        symbols_.insert(symbol);
        probes_[symbol] = SymbolProbeState{};
        printf("[PROBE_CTRL] Registered %s (fixed qty=%.6f)\n", symbol, PROBE_QTY);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ENABLE/DISABLE - Only enable when engine is stable!
    // ═══════════════════════════════════════════════════════════════════════
    
    void enable() {
        if (!enabled_) {
            enabled_ = true;
            printf("[PROBE_CTRL] ✓ ENABLED - probes will start after interval\n");
        }
    }
    
    void disable() {
        if (enabled_) {
            enabled_ = false;
            printf("[PROBE_CTRL] ✗ DISABLED - all probes halted\n");
        }
    }
    
    bool isEnabled() const { return enabled_; }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MAIN TICK - Call this once per symbol per main loop iteration
    // ═══════════════════════════════════════════════════════════════════════
    
    void onTick(const char* symbol, double mid_price) {
        // v4.9.30: Diagnostic logging
        static uint64_t tick_diag_counter = 0;
        tick_diag_counter++;
        bool verbose = (tick_diag_counter % 100 == 1);  // Log every ~10s
        
        if (verbose) {
            printf("[PROBE_CTRL] onTick(%s, %.2f) enabled=%d send_cb=%d\n",
                   symbol, mid_price, enabled_ ? 1 : 0, send_cb_ ? 1 : 0);
        }
        
        if (!enabled_) {
            if (verbose) printf("[PROBE_CTRL] BLOCKED: not enabled\n");
            return;
        }
        if (!send_cb_) {
            if (verbose) printf("[PROBE_CTRL] BLOCKED: no send callback\n");
            return;
        }
        
        // Check if symbol is registered
        if (symbols_.find(symbol) == symbols_.end()) {
            if (verbose) printf("[PROBE_CTRL] BLOCKED: symbol %s not registered\n", symbol);
            return;
        }
        
        auto probe_it = probes_.find(symbol);
        if (probe_it == probes_.end()) {
            if (verbose) printf("[PROBE_CTRL] BLOCKED: no probe state for %s\n", symbol);
            return;
        }
        
        SymbolProbeState& ps = probe_it->second;
        uint64_t now_ns = now_monotonic_ns();
        
        // Already have enough probes?
        if (ps.acks_received >= TARGET_PROBES) {
            if (verbose) printf("[PROBE_CTRL] %s: already have %u probes (target=%u)\n", 
                               symbol, ps.acks_received, TARGET_PROBES);
            return;
        }
        
        if (verbose) {
            printf("[PROBE_CTRL] %s: state=%s acks=%u processing...\n",
                   symbol, probeStateStr(ps.state), ps.acks_received);
        }
        
        // State machine
        switch (ps.state) {
            case ProbeState::IDLE:
                handleIdle(symbol, ps, mid_price, now_ns);
                break;
                
            case ProbeState::SENT:
                handleSent(symbol, ps, now_ns);
                break;
                
            case ProbeState::CANCELLING:
                handleCancelling(symbol, ps, now_ns);
                break;
                
            case ProbeState::COOLDOWN:
                handleCooldown(symbol, ps, now_ns);
                break;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // CALLBACKS - Wire these from CfdEngine (v4.11.0: crypto removed)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Called when order ACK received (status=NEW)
    void onOrderAck(uint64_t client_order_id, uint64_t exchange_order_id) {
        // Find which symbol this belongs to
        for (auto& [symbol, ps] : probes_) {
            if (ps.state == ProbeState::SENT && ps.client_order_id == client_order_id) {
                uint64_t now_ns = now_monotonic_ns();
                uint64_t latency_ns = now_ns - ps.sent_ts_ns;
                
                ps.exchange_order_id = exchange_order_id;
                ps.acks_received++;
                
                double latency_ms = latency_ns / 1'000'000.0;
                printf("[PROBE_CTRL] ✓ %s ACK latency=%.3fms (sample %u/%u)\n",
                       symbol.c_str(), latency_ms, ps.acks_received, TARGET_PROBES);
                
                // Report latency
                if (latency_cb_) {
                    latency_cb_(symbol.c_str(), latency_ns);
                }
                
                // IOC order auto-cancels, so go straight to cooldown
                transitionTo(ps, ProbeState::COOLDOWN, now_ns);
                
                // Cancel the order just in case (belt and suspenders)
                if (cancel_cb_ && exchange_order_id > 0) {
                    cancel_cb_(symbol.c_str(), exchange_order_id);
                }
                
                return;
            }
        }
    }
    
    // Called when order rejected
    void onOrderReject(uint64_t client_order_id, int error_code, const char* reason) {
        for (auto& [symbol, ps] : probes_) {
            if (ps.state == ProbeState::SENT && ps.client_order_id == client_order_id) {
                uint64_t now_ns = now_monotonic_ns();
                ps.rejects_received++;
                
                printf("[PROBE_CTRL] ✗ %s REJECT error=%d reason=%s\n",
                       symbol.c_str(), error_code, reason ? reason : "unknown");
                
                // Go to cooldown (backoff)
                transitionTo(ps, ProbeState::COOLDOWN, now_ns);
                return;
            }
        }
    }
    
    // Called when cancel ACK received
    void onCancelAck(uint64_t exchange_order_id) {
        for (auto& [symbol, ps] : probes_) {
            if (ps.state == ProbeState::CANCELLING && ps.exchange_order_id == exchange_order_id) {
                uint64_t now_ns = now_monotonic_ns();
                printf("[PROBE_CTRL] %s cancel ACK received\n", symbol.c_str());
                transitionTo(ps, ProbeState::COOLDOWN, now_ns);
                return;
            }
        }
    }
    
    // Called on WebSocket disconnect - reset all in-flight state
    void onDisconnect() {
        printf("[PROBE_CTRL] WS disconnect - resetting all probe state\n");
        for (auto& [symbol, ps] : probes_) {
            ps.reset();
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY
    // ═══════════════════════════════════════════════════════════════════════
    
    uint32_t getAcks(const char* symbol) const {
        auto it = probes_.find(symbol);
        return (it != probes_.end()) ? it->second.acks_received : 0;
    }
    
    bool isComplete(const char* symbol) const {
        auto it = probes_.find(symbol);
        return (it != probes_.end()) ? it->second.acks_received >= TARGET_PROBES : false;
    }
    
    bool allComplete() const {
        if (probes_.empty()) return false;
        for (const auto& [sym, ps] : probes_) {
            if (ps.acks_received < TARGET_PROBES) return false;
        }
        return true;
    }
    
    const char* getState(const char* symbol) const {
        auto it = probes_.find(symbol);
        return (it != probes_.end()) ? probeStateStr(it->second.state) : "UNKNOWN";
    }
    
private:
    // ─────────────────────────────────────────────────────────────────────
    // State Handlers - NO CONFIG, uses fixed PROBE_QTY
    // ─────────────────────────────────────────────────────────────────────
    
    void handleIdle(const char* symbol, SymbolProbeState& ps, 
                    double mid_price, uint64_t now_ns) {
        // Check interval since last probe
        if (ps.state_enter_ts_ns > 0 && 
            (now_ns - ps.state_enter_ts_ns) < PROBE_INTERVAL_NS) {
            // v4.9.30: Log interval wait occasionally
            static uint64_t interval_log_counter = 0;
            if (++interval_log_counter % 200 == 1) {
                uint64_t remaining_ns = PROBE_INTERVAL_NS - (now_ns - ps.state_enter_ts_ns);
                printf("[PROBE_CTRL] %s IDLE waiting: %.1fs remaining\n", 
                       symbol, remaining_ns / 1'000'000'000.0);
            }
            return;  // Still waiting for interval
        }
        
        // Calculate probe price (10% below mid)
        double probe_price = mid_price * (1.0 - PRICE_OFFSET_PCT);
        
        // Generate client order ID
        uint64_t client_id = next_client_id_++;
        
        // Send probe with IOC, using fixed PROBE_QTY
        printf("[PROBE_CTRL] %s sending IOC probe @ %.2f qty=%.6f\n",
               symbol, probe_price, PROBE_QTY);
        
        bool sent = send_cb_(symbol, probe_price, PROBE_QTY, client_id, true);  // IOC=true
        
        if (sent) {
            ps.client_order_id = client_id;
            ps.sent_ts_ns = now_ns;
            transitionTo(ps, ProbeState::SENT, now_ns);
            printf("[PROBE_CTRL] %s SENT probe id=%llu [WAITING FOR ACK]\n",
                   symbol, static_cast<unsigned long long>(client_id));
        } else {
            printf("[PROBE_CTRL] %s send FAILED - will retry\n", symbol);
            // Stay in IDLE, will retry next interval
        }
    }
    
    void handleSent(const char* symbol, SymbolProbeState& ps, uint64_t now_ns) {
        // Check timeout
        if ((now_ns - ps.sent_ts_ns) >= PROBE_TIMEOUT_NS) {
            printf("[PROBE_CTRL] %s TIMEOUT after 3s - sending cancel\n", symbol);
            ps.timeouts++;
            
            // Send cancel if we have exchange order ID
            if (cancel_cb_ && ps.exchange_order_id > 0) {
                cancel_cb_(symbol, ps.exchange_order_id);
                transitionTo(ps, ProbeState::CANCELLING, now_ns);
            } else {
                // No exchange ID (never got ACK) - go straight to cooldown
                transitionTo(ps, ProbeState::COOLDOWN, now_ns);
            }
        }
        // Otherwise just wait for ACK/reject
    }
    
    void handleCancelling(const char* symbol, SymbolProbeState& ps, uint64_t now_ns) {
        // Check cancel timeout
        if ((now_ns - ps.state_enter_ts_ns) >= CANCEL_TIMEOUT_NS) {
            printf("[PROBE_CTRL] %s cancel timeout - forcing to cooldown\n", symbol);
            transitionTo(ps, ProbeState::COOLDOWN, now_ns);
        }
        // Otherwise wait for cancel ACK
    }
    
    void handleCooldown(const char* symbol, SymbolProbeState& ps, uint64_t now_ns) {
        if ((now_ns - ps.state_enter_ts_ns) >= COOLDOWN_NS) {
            ps.reset();
            ps.state_enter_ts_ns = now_ns;  // For next interval check
            printf("[PROBE_CTRL] %s cooldown complete - ready for next probe\n", symbol);
        }
    }
    
    void transitionTo(SymbolProbeState& ps, ProbeState new_state, uint64_t now_ns) {
        ps.state = new_state;
        ps.state_enter_ts_ns = now_ns;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Utilities
    // ─────────────────────────────────────────────────────────────────────
    
    static uint64_t now_monotonic_ns() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return uint64_t(ts.tv_sec) * 1'000'000'000ULL + ts.tv_nsec;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Data - NO CONFIG TYPES
    // ─────────────────────────────────────────────────────────────────────
    
    bool enabled_;
    std::atomic<uint64_t> next_client_id_;
    
    std::unordered_set<std::string> symbols_;              // Registered symbols
    std::unordered_map<std::string, SymbolProbeState> probes_;  // Per-symbol state
    
    ProbeSendCallback send_cb_;
    ProbeCancelCallback cancel_cb_;
    ProbeLatencyCallback latency_cb_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Instance
// ─────────────────────────────────────────────────────────────────────────────
inline ProbeController& getProbeController() {
    static ProbeController instance;
    return instance;
}

} // namespace Chimera
