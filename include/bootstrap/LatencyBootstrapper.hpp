// ═══════════════════════════════════════════════════════════════════════════════
// include/bootstrap/LatencyBootstrapper.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.10: INSTITUTIONAL LATENCY BOOTSTRAP ENGINE
//
// PURPOSE:
// Measures REAL order ACK latency by sending probe orders far from market,
// then immediately cancelling them. No fills, no PnL risk.
//
// ARCHITECTURE:
// 1. On startup: Enter BOOTSTRAP mode
// 2. Send limit BUY orders 10% below mid price
// 3. On ACK: Record latency, send cancel
// 4. On CANCEL_ACK: Record cancel latency
// 5. After N probes: Compute percentiles, transition to LIVE
//
// This is what real trading systems do.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <mutex>

#include "runtime/SystemMode.hpp"
#include "latency/HotPathLatencyTracker.hpp"

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Probe Order State
// ─────────────────────────────────────────────────────────────────────────────
struct ProbeOrder {
    uint64_t client_order_id = 0;
    uint64_t exchange_order_id = 0;
    uint64_t send_ns = 0;
    uint64_t ack_ns = 0;
    uint64_t cancel_send_ns = 0;
    uint64_t cancel_ack_ns = 0;
    uint16_t symbol_id = 0;
    bool acked = false;
    bool cancelled = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Latency Statistics
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolLatencyStats {
    std::vector<uint64_t> ack_latencies_ns;
    std::vector<uint64_t> cancel_latencies_ns;
    
    // Computed percentiles
    double p50_ack_ms = 0.0;
    double p80_ack_ms = 0.0;
    double p95_ack_ms = 0.0;
    double p50_cancel_ms = 0.0;
    double stability = 0.0;  // 1.0 = stable, 0.0 = chaotic
    
    // Bootstrap state
    SymbolBootstrapState boot_state;
    ProbeConfig config;
    
    void computePercentiles() {
        if (ack_latencies_ns.empty()) return;
        
        std::vector<uint64_t> sorted = ack_latencies_ns;
        std::sort(sorted.begin(), sorted.end());
        
        auto pick = [&](double q) -> double {
            size_t idx = static_cast<size_t>(q * (sorted.size() - 1));
            return sorted[idx] / 1'000'000.0;  // ns -> ms
        };
        
        p50_ack_ms = pick(0.50);
        p80_ack_ms = pick(0.80);
        p95_ack_ms = pick(0.95);
        
        // Stability = 1 - (jitter / p50)
        double jitter = p80_ack_ms - p50_ack_ms;
        stability = (p50_ack_ms > 0) ? std::max(0.0, 1.0 - jitter / p50_ack_ms) : 0.0;
        
        // Cancel latencies
        if (!cancel_latencies_ns.empty()) {
            std::vector<uint64_t> cancel_sorted = cancel_latencies_ns;
            std::sort(cancel_sorted.begin(), cancel_sorted.end());
            size_t idx = static_cast<size_t>(0.50 * (cancel_sorted.size() - 1));
            p50_cancel_ms = cancel_sorted[idx] / 1'000'000.0;
        }
    }
    
    bool isReady() const {
        return boot_state.probes_acked >= config.min_confidence;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Latency Bootstrap Engine
// ─────────────────────────────────────────────────────────────────────────────
class LatencyBootstrapper {
public:
    // Callback types for order sending
    using SendProbeCallback = std::function<bool(
        uint16_t symbol_id,
        const char* symbol,
        double price,
        double qty,
        uint64_t client_order_id
    )>;
    
    using CancelProbeCallback = std::function<bool(
        uint16_t symbol_id,
        const char* symbol,
        uint64_t exchange_order_id
    )>;
    
    LatencyBootstrapper() : next_client_id_(1'000'000) {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════════
    
    void registerSymbol(uint16_t symbol_id, const char* symbol) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        SymbolLatencyStats& stats = symbol_stats_[symbol_id];
        stats.config = getProbeConfig(symbol);
        stats.boot_state.reset();
        
        strncpy(symbol_names_[symbol_id], symbol, sizeof(symbol_names_[symbol_id]) - 1);
        
        printf("[BOOTSTRAP] Registered symbol %s (id=%u) - target=%u probes, min=%u confidence\n",
               symbol, symbol_id, stats.config.target_probes, stats.config.min_confidence);
    }
    
    void setSendCallback(SendProbeCallback cb) { send_cb_ = std::move(cb); }
    void setCancelCallback(CancelProbeCallback cb) { cancel_cb_ = std::move(cb); }
    
    // ═══════════════════════════════════════════════════════════════════════
    // PROBE LIFECYCLE
    // v4.9.12: Added exponential backoff on WebSocket failures
    // ═══════════════════════════════════════════════════════════════════════
    
    // Call this every tick to potentially send a probe
    bool maybeProbe(uint16_t symbol_id, double mid_price, uint64_t now_ns) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        // v4.9.27: GLOBAL rate limit - max 2 orders/sec total across all symbols
        // Binance WS API limit is 1200/10min = 2/sec
        constexpr uint64_t GLOBAL_MIN_SPACING_NS = 500'000'000ULL;  // 500ms
        if (now_ns - last_global_probe_ns_ < GLOBAL_MIN_SPACING_NS) {
            return false;  // Global rate limit
        }
        
        auto it = symbol_stats_.find(symbol_id);
        if (it == symbol_stats_.end()) return false;
        
        SymbolLatencyStats& stats = it->second;
        
        // Already have enough probes?
        if (stats.isReady()) {
            if (stats.boot_state.mode == SystemMode::BOOTSTRAP) {
                stats.boot_state.mode = SystemMode::LIVE;
                stats.boot_state.ready = true;
                stats.computePercentiles();
                
                printf("[BOOTSTRAP] Symbol %s -> LIVE (p50=%.2fms p80=%.2fms stability=%.2f)\n",
                       symbol_names_[symbol_id], stats.p50_ack_ms, stats.p80_ack_ms, stats.stability);
                
                checkGlobalTransition();
            }
            return false;
        }
        
        // v4.9.12: Check backoff period (exponential backoff on WS failures)
        if (stats.boot_state.inBackoff(now_ns)) {
            return false;  // Still in backoff, silently skip
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // v4.9.27: ACK-GATING - CRITICAL FIX
        // Only ONE probe in flight per symbol. Wait for ACK or timeout.
        // This prevents hammering Binance WS-API and causing disconnects.
        // ═══════════════════════════════════════════════════════════════════════
        if (stats.boot_state.probe_in_flight) {
            if (stats.boot_state.probeTimedOut(now_ns)) {
                // Probe timed out - clear and allow retry
                printf("[PROBE_TIMEOUT] %s probe #%llu timed out after 3s - no ACK received\n",
                       symbol_names_[symbol_id],
                       static_cast<unsigned long long>(stats.boot_state.in_flight_client_id));
                stats.boot_state.clearInFlight();
                // Don't send immediately - let spacing kick in next iteration
                return false;
            } else {
                // Probe still in flight, waiting for ACK
                return false;
            }
        }
        
        // Respect spacing
        uint64_t spacing_ns = uint64_t(stats.config.spacing_ms) * 1'000'000ULL;
        if (now_ns - stats.boot_state.last_probe_ns < spacing_ns) {
            return false;
        }
        
        // Already sent enough probes (waiting for ACKs)?
        if (stats.boot_state.probes_sent >= stats.config.target_probes) {
            return false;
        }
        
        // Calculate probe price (far below market)
        double probe_price = mid_price * (1.0 - stats.config.price_offset_pct);
        
        // Generate client order ID
        uint64_t client_id = next_client_id_++;
        
        // Send the probe
        if (send_cb_) {
            ProbeOrder probe;
            probe.client_order_id = client_id;
            probe.send_ns = now_ns_monotonic();  // Timestamp BEFORE send
            probe.symbol_id = symbol_id;
            
            bool sent = send_cb_(symbol_id, symbol_names_[symbol_id], 
                                 probe_price, stats.config.probe_qty, client_id);
            
            if (sent) {
                pending_probes_[client_id] = probe;
                stats.boot_state.probes_sent++;
                stats.boot_state.last_probe_ns = now_ns;
                stats.boot_state.recordSendSuccess();  // v4.9.12: Reset backoff on success
                last_global_probe_ns_ = now_ns;  // v4.9.27: Update global rate limiter
                
                // v4.9.27: Mark probe as in-flight (ACK gating)
                stats.boot_state.markProbeInFlight(client_id, now_ns);
                
                printf("[PROBE_SENT] %s probe #%u @ %.2f (%.1f%% below mid) [WAITING FOR ACK]\n",
                       symbol_names_[symbol_id], stats.boot_state.probes_sent,
                       probe_price, stats.config.price_offset_pct * 100);
                
                return true;
            } else {
                // v4.9.12: WebSocket send failed - apply exponential backoff
                stats.boot_state.recordSendFail(now_ns);
                
                // Log only when entering new backoff level
                double backoff_sec = stats.boot_state.calculateBackoffNs() / 1'000'000'000.0;
                printf("[PROBE_BACKOFF] %s - WS send failed, backing off %.0fs (fail #%u)\n",
                       symbol_names_[symbol_id], backoff_sec, stats.boot_state.consecutive_send_fails);
            }
        }
        
        return false;
    }
    
    // Called when order ACK (NEW status) received
    void onOrderAck(uint64_t client_order_id, uint64_t exchange_order_id) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = pending_probes_.find(client_order_id);
        if (it == pending_probes_.end()) return;
        
        ProbeOrder& probe = it->second;
        probe.ack_ns = now_ns_monotonic();
        probe.exchange_order_id = exchange_order_id;
        probe.acked = true;
        
        // Record ACK latency
        uint64_t ack_latency_ns = probe.ack_ns - probe.send_ns;
        
        auto sym_it = symbol_stats_.find(probe.symbol_id);
        if (sym_it != symbol_stats_.end()) {
            sym_it->second.ack_latencies_ns.push_back(ack_latency_ns);
            sym_it->second.boot_state.probes_acked++;
            
            // v4.9.27: Clear in-flight status - allows next probe
            sym_it->second.boot_state.clearInFlight();
            
            double ack_ms = ack_latency_ns / 1'000'000.0;
            printf("[PROBE_ACK] %s ACK latency: %.3fms (sample %zu) [IN-FLIGHT CLEARED]\n",
                   symbol_names_[probe.symbol_id], ack_ms,
                   sym_it->second.ack_latencies_ns.size());
        }
        
        // Immediately send cancel
        if (cancel_cb_ && exchange_order_id > 0) {
            probe.cancel_send_ns = now_ns_monotonic();
            cancel_cb_(probe.symbol_id, symbol_names_[probe.symbol_id], exchange_order_id);
        }
    }
    
    // Called when cancel ACK received
    void onCancelAck(uint64_t exchange_order_id) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        // Find probe by exchange order ID
        for (auto& [client_id, probe] : pending_probes_) {
            if (probe.exchange_order_id == exchange_order_id && probe.acked && !probe.cancelled) {
                probe.cancel_ack_ns = now_ns_monotonic();
                probe.cancelled = true;
                
                // Record cancel latency
                if (probe.cancel_send_ns > 0) {
                    uint64_t cancel_latency_ns = probe.cancel_ack_ns - probe.cancel_send_ns;
                    
                    auto sym_it = symbol_stats_.find(probe.symbol_id);
                    if (sym_it != symbol_stats_.end()) {
                        sym_it->second.cancel_latencies_ns.push_back(cancel_latency_ns);
                        sym_it->second.boot_state.probes_cancelled++;
                        
                        double cancel_ms = cancel_latency_ns / 1'000'000.0;
                        printf("[PROBE_CANCEL_ACK] %s cancel latency: %.3fms\n",
                               symbol_names_[probe.symbol_id], cancel_ms);
                    }
                }
                
                // Clean up completed probe
                pending_probes_.erase(client_id);
                break;
            }
        }
    }
    
    // Called on order reject (probe failed)
    void onOrderReject(uint64_t client_order_id) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        // v4.9.27: Clear in-flight status on reject
        auto it = pending_probes_.find(client_order_id);
        if (it != pending_probes_.end()) {
            auto sym_it = symbol_stats_.find(it->second.symbol_id);
            if (sym_it != symbol_stats_.end()) {
                sym_it->second.boot_state.clearInFlight();
                printf("[PROBE_REJECT] %s probe rejected - in-flight cleared\n",
                       symbol_names_[it->second.symbol_id]);
            }
        }
        
        pending_probes_.erase(client_order_id);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY INTERFACE
    // ═══════════════════════════════════════════════════════════════════════
    
    bool isSymbolReady(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return it != symbol_stats_.end() && it->second.isReady();
    }
    
    SystemMode symbolMode(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.boot_state.mode : SystemMode::BOOTSTRAP;
    }
    
    SymbolLatencyStats getStats(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        if (it != symbol_stats_.end()) {
            return it->second;
        }
        return {};
    }
    
    double getP50AckMs(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.p50_ack_ms : 0.0;
    }
    
    double getP80AckMs(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.p80_ack_ms : 0.0;
    }
    
    double getStability(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.stability : 0.0;
    }
    
    uint32_t getProbesSent(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.boot_state.probes_sent : 0;
    }
    
    uint32_t getProbesAcked(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.boot_state.probes_acked : 0;
    }
    
    uint32_t getTargetProbes(uint16_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbol_stats_.find(symbol_id);
        return (it != symbol_stats_.end()) ? it->second.config.target_probes : 0;
    }
    
    // Check if ALL symbols are ready
    bool allSymbolsReady() const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (symbol_stats_.empty()) return false;
        for (const auto& [id, stats] : symbol_stats_) {
            if (!stats.isReady()) return false;
        }
        return true;
    }
    
    // Get bootstrap progress string for GUI
    void getProgressString(char* buf, size_t buf_size) const {
        std::lock_guard<std::mutex> lock(mtx_);
        
        int ready_count = 0;
        int total_count = 0;
        
        for (const auto& [id, stats] : symbol_stats_) {
            total_count++;
            if (stats.isReady()) ready_count++;
        }
        
        snprintf(buf, buf_size, "BOOTSTRAP %d/%d symbols ready", ready_count, total_count);
    }
    
private:
    void checkGlobalTransition() {
        // If all symbols ready, transition global mode
        bool all_ready = true;
        for (const auto& [id, stats] : symbol_stats_) {
            if (!stats.isReady()) {
                all_ready = false;
                break;
            }
        }
        
        if (all_ready && !symbol_stats_.empty()) {
            getSystemMode().setGlobalMode(SystemMode::LIVE);
            printf("[BOOTSTRAP] ALL SYMBOLS READY - System entering LIVE mode\n");
        }
    }
    
    mutable std::mutex mtx_;
    std::unordered_map<uint16_t, SymbolLatencyStats> symbol_stats_;
    std::unordered_map<uint64_t, ProbeOrder> pending_probes_;
    char symbol_names_[64][32] = {};  // symbol_id -> name
    std::atomic<uint64_t> next_client_id_;
    uint64_t last_global_probe_ns_ = 0;  // v4.9.27: Global rate limit tracker
    
    SendProbeCallback send_cb_;
    CancelProbeCallback cancel_cb_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Bootstrapper Instance
// ─────────────────────────────────────────────────────────────────────────────
inline LatencyBootstrapper& getBootstrapper() {
    static LatencyBootstrapper instance;
    return instance;
}

} // namespace Chimera
