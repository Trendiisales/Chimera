// =============================================================================
// SpeedEdgeMetrics.hpp - v4.6.0 - SPEED EDGE DASHBOARD INDICATORS
// =============================================================================
// PURPOSE: Track whether speed is actually providing value
//
// FIVE LIVE INDICATORS (if any are RED, speed is NOT helping):
//   1. LATENCY EDGE: median_peer_latency - our_latency (GREEN if ≥ 1.5ms)
//   2. SCRATCH SAVED: trades scratched < stop that would have lost
//   3. EARLY ENTRY: our_entry_time - median_market_entry (GREEN if < -80ms)
//   4. BURST CAPTURE: captured_move / total_burst_move (GREEN if > 35%)
//   5. SPEED ADJUSTED EV: EV_fast - EV_slow (must be positive)
//
// DESIGN PRINCIPLES:
//   - If SCRATCH_SAVED counter is zero, speed is not adding value
//   - If EARLY_ENTRY is positive, we're late - speed not helping
//   - If BURST_CAPTURE < 20%, entries are too slow
//   - All metrics update in real-time
//   - Negative indicators trigger automatic mode reversion
//
// USAGE:
//   auto& metrics = SpeedEdgeMetrics::instance();
//   metrics.onEntry(symbol, our_entry_ns, market_median_ns, burst_total_move);
//   metrics.onScratch(symbol, would_have_lost);
//   metrics.onExit(symbol, captured_move);
//
//   // Check if speed is working
//   if (!metrics.isSpeedEdgePositive()) {
//       disableAggressiveMode();
//   }
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdio>

namespace Chimera {
namespace Speed {

// =============================================================================
// EDGE STATUS - Traffic light system
// =============================================================================
enum class EdgeStatus : uint8_t {
    GREEN = 0,      // Speed is helping
    YELLOW = 1,     // Marginal
    RED = 2         // Speed is NOT helping
};

inline const char* edge_status_str(EdgeStatus s) {
    switch (s) {
        case EdgeStatus::GREEN:  return "GREEN";
        case EdgeStatus::YELLOW: return "YELLOW";
        case EdgeStatus::RED:    return "RED";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// INDICATOR 1: LATENCY EDGE
// =============================================================================
// EDGE = median_peer_latency - our_latency
// GREEN: ≥ 1.5ms | YELLOW: 0.5-1.5ms | RED: < 0.5ms
struct LatencyEdgeIndicator {
    double our_latency_ms = 0.0;
    double peer_median_ms = 5.0;        // Assumed peer latency (update from market)
    
    double edge() const { return peer_median_ms - our_latency_ms; }
    
    EdgeStatus status() const {
        double e = edge();
        if (e >= 1.5) return EdgeStatus::GREEN;
        if (e >= 0.5) return EdgeStatus::YELLOW;
        return EdgeStatus::RED;
    }
    
    void update(double our_ms, double peer_ms = 0.0) {
        our_latency_ms = our_ms;
        if (peer_ms > 0.0) peer_median_ms = peer_ms;
    }
    
    void print() const {
        printf("[LATENCY-EDGE] our=%.2fms peer=%.2fms edge=%.2fms [%s]\n",
               our_latency_ms, peer_median_ms, edge(), edge_status_str(status()));
    }
};

// =============================================================================
// INDICATOR 2: SCRATCH SAVED COUNTER
// =============================================================================
// Tracks trades scratched before stop that would have lost
// If this is ZERO over a session, speed is not adding value
struct ScratchSavedIndicator {
    std::atomic<uint32_t> scratches_total{0};       // Total scratches
    std::atomic<uint32_t> scratches_saved{0};       // Scratches that would have hit stop
    std::atomic<double> saved_pnl_bps{0.0};         // Cumulative PnL saved
    
    void recordScratch(bool would_have_lost, double potential_loss_bps = 0.0) {
        scratches_total.fetch_add(1, std::memory_order_relaxed);
        if (would_have_lost) {
            scratches_saved.fetch_add(1, std::memory_order_relaxed);
            // Atomic add for double (approximate, fine for metrics)
            double current = saved_pnl_bps.load(std::memory_order_relaxed);
            while (!saved_pnl_bps.compare_exchange_weak(current, current + potential_loss_bps,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
        }
    }
    
    double saveRate() const {
        uint32_t total = scratches_total.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        return static_cast<double>(scratches_saved.load(std::memory_order_relaxed)) / total;
    }
    
    EdgeStatus status() const {
        // If we have enough scratches but none saved, speed isn't helping
        uint32_t total = scratches_total.load(std::memory_order_relaxed);
        if (total < 10) return EdgeStatus::YELLOW;  // Not enough data
        
        double rate = saveRate();
        if (rate >= 0.30) return EdgeStatus::GREEN;   // 30%+ scratches saved value
        if (rate >= 0.10) return EdgeStatus::YELLOW;
        return EdgeStatus::RED;
    }
    
    void reset() {
        scratches_total.store(0, std::memory_order_relaxed);
        scratches_saved.store(0, std::memory_order_relaxed);
        saved_pnl_bps.store(0.0, std::memory_order_relaxed);
    }
    
    void print() const {
        printf("[SCRATCH-SAVED] total=%u saved=%u rate=%.1f%% pnl_saved=%.1fbps [%s]\n",
               scratches_total.load(), scratches_saved.load(),
               saveRate() * 100.0, saved_pnl_bps.load(),
               edge_status_str(status()));
    }
};

// =============================================================================
// INDICATOR 3: EARLY ENTRY ADVANTAGE
// =============================================================================
// Δt = our_entry_time - median_market_entry
// Positive expectancy if Δt < -80ms (we're 80ms early)
struct EarlyEntryIndicator {
    std::atomic<int64_t> total_delta_ns{0};
    std::atomic<uint32_t> entry_count{0};
    
    void recordEntry(int64_t our_entry_ns, int64_t market_median_ns) {
        int64_t delta = our_entry_ns - market_median_ns;
        
        // Running average (simplified)
        entry_count.fetch_add(1, std::memory_order_relaxed);
        
        // Update total (for average calculation)
        int64_t current = total_delta_ns.load(std::memory_order_relaxed);
        while (!total_delta_ns.compare_exchange_weak(current, current + delta,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    
    double avgDeltaMs() const {
        uint32_t count = entry_count.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(total_delta_ns.load(std::memory_order_relaxed)) / count / 1'000'000.0;
    }
    
    EdgeStatus status() const {
        uint32_t count = entry_count.load(std::memory_order_relaxed);
        if (count < 10) return EdgeStatus::YELLOW;  // Not enough data
        
        double delta_ms = avgDeltaMs();
        if (delta_ms < -80.0) return EdgeStatus::GREEN;    // 80ms+ early
        if (delta_ms < -20.0) return EdgeStatus::YELLOW;   // 20-80ms early
        return EdgeStatus::RED;                             // Late or even
    }
    
    void reset() {
        total_delta_ns.store(0, std::memory_order_relaxed);
        entry_count.store(0, std::memory_order_relaxed);
    }
    
    void print() const {
        printf("[EARLY-ENTRY] avg_delta=%.1fms entries=%u [%s]\n",
               avgDeltaMs(), entry_count.load(),
               edge_status_str(status()));
    }
};

// =============================================================================
// INDICATOR 4: BURST CAPTURE EFFICIENCY
// =============================================================================
// captured_move / total_burst_move
// Healthy: > 35% | Below 20% → late entries, slow confirmation
struct BurstCaptureIndicator {
    std::atomic<double> total_captured_bps{0.0};
    std::atomic<double> total_burst_bps{0.0};
    std::atomic<uint32_t> burst_count{0};
    
    void recordBurst(double captured_bps, double total_bps) {
        if (total_bps <= 0.0) return;
        
        burst_count.fetch_add(1, std::memory_order_relaxed);
        
        // Atomic add for doubles
        double current = total_captured_bps.load(std::memory_order_relaxed);
        while (!total_captured_bps.compare_exchange_weak(current, current + captured_bps,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
        
        current = total_burst_bps.load(std::memory_order_relaxed);
        while (!total_burst_bps.compare_exchange_weak(current, current + total_bps,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    
    double captureRate() const {
        double total = total_burst_bps.load(std::memory_order_relaxed);
        if (total <= 0.0) return 0.0;
        return total_captured_bps.load(std::memory_order_relaxed) / total;
    }
    
    EdgeStatus status() const {
        uint32_t count = burst_count.load(std::memory_order_relaxed);
        if (count < 5) return EdgeStatus::YELLOW;  // Not enough data
        
        double rate = captureRate();
        if (rate >= 0.35) return EdgeStatus::GREEN;
        if (rate >= 0.20) return EdgeStatus::YELLOW;
        return EdgeStatus::RED;
    }
    
    void reset() {
        total_captured_bps.store(0.0, std::memory_order_relaxed);
        total_burst_bps.store(0.0, std::memory_order_relaxed);
        burst_count.store(0, std::memory_order_relaxed);
    }
    
    void print() const {
        printf("[BURST-CAPTURE] captured=%.1fbps total=%.1fbps rate=%.1f%% bursts=%u [%s]\n",
               total_captured_bps.load(), total_burst_bps.load(),
               captureRate() * 100.0, burst_count.load(),
               edge_status_str(status()));
    }
};

// =============================================================================
// INDICATOR 5: SPEED-ADJUSTED EXPECTANCY
// =============================================================================
// EV_fast - EV_slow
// If EV_fast ≤ EV_slow → revert aggression
struct SpeedExpectancyIndicator {
    // Fast mode (current aggressive thresholds)
    std::atomic<double> fast_total_pnl_bps{0.0};
    std::atomic<uint32_t> fast_trades{0};
    
    // Slow mode (conservative thresholds - for comparison)
    std::atomic<double> slow_total_pnl_bps{0.0};
    std::atomic<uint32_t> slow_trades{0};
    
    void recordFastTrade(double pnl_bps) {
        fast_trades.fetch_add(1, std::memory_order_relaxed);
        double current = fast_total_pnl_bps.load(std::memory_order_relaxed);
        while (!fast_total_pnl_bps.compare_exchange_weak(current, current + pnl_bps,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    
    void recordSlowTrade(double pnl_bps) {
        slow_trades.fetch_add(1, std::memory_order_relaxed);
        double current = slow_total_pnl_bps.load(std::memory_order_relaxed);
        while (!slow_total_pnl_bps.compare_exchange_weak(current, current + pnl_bps,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    
    double fastEV() const {
        uint32_t count = fast_trades.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return fast_total_pnl_bps.load(std::memory_order_relaxed) / count;
    }
    
    double slowEV() const {
        uint32_t count = slow_trades.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return slow_total_pnl_bps.load(std::memory_order_relaxed) / count;
    }
    
    double evDelta() const { return fastEV() - slowEV(); }
    
    EdgeStatus status() const {
        uint32_t fast_n = fast_trades.load(std::memory_order_relaxed);
        uint32_t slow_n = slow_trades.load(std::memory_order_relaxed);
        
        if (fast_n < 20 || slow_n < 20) return EdgeStatus::YELLOW;
        
        double delta = evDelta();
        if (delta > 0.3) return EdgeStatus::GREEN;   // Fast mode >0.3 bps better
        if (delta > 0.0) return EdgeStatus::YELLOW;  // Marginal
        return EdgeStatus::RED;                       // Slow is better
    }
    
    void reset() {
        fast_total_pnl_bps.store(0.0, std::memory_order_relaxed);
        fast_trades.store(0, std::memory_order_relaxed);
        slow_total_pnl_bps.store(0.0, std::memory_order_relaxed);
        slow_trades.store(0, std::memory_order_relaxed);
    }
    
    void print() const {
        printf("[SPEED-EV] fast=%.2fbps(%ut) slow=%.2fbps(%ut) delta=%.2fbps [%s]\n",
               fastEV(), fast_trades.load(),
               slowEV(), slow_trades.load(),
               evDelta(), edge_status_str(status()));
    }
};

// =============================================================================
// AGGREGATE SPEED EDGE METRICS (Singleton)
// =============================================================================
class SpeedEdgeMetrics {
public:
    static SpeedEdgeMetrics& instance() {
        static SpeedEdgeMetrics inst;
        return inst;
    }
    
    // === INDICATORS ===
    LatencyEdgeIndicator latency;
    ScratchSavedIndicator scratch;
    EarlyEntryIndicator early_entry;
    BurstCaptureIndicator burst_capture;
    SpeedExpectancyIndicator expectancy;
    
    // === AGGREGATE STATUS ===
    bool isSpeedEdgePositive() const {
        // All indicators must be GREEN or YELLOW
        // If ANY is RED, speed edge is negative
        if (latency.status() == EdgeStatus::RED) return false;
        if (scratch.status() == EdgeStatus::RED) return false;
        if (early_entry.status() == EdgeStatus::RED) return false;
        if (burst_capture.status() == EdgeStatus::RED) return false;
        if (expectancy.status() == EdgeStatus::RED) return false;
        return true;
    }
    
    EdgeStatus aggregateStatus() const {
        // Count RED/YELLOW/GREEN
        int red = 0, yellow = 0;
        
        if (latency.status() == EdgeStatus::RED) red++;
        else if (latency.status() == EdgeStatus::YELLOW) yellow++;
        
        if (scratch.status() == EdgeStatus::RED) red++;
        else if (scratch.status() == EdgeStatus::YELLOW) yellow++;
        
        if (early_entry.status() == EdgeStatus::RED) red++;
        else if (early_entry.status() == EdgeStatus::YELLOW) yellow++;
        
        if (burst_capture.status() == EdgeStatus::RED) red++;
        else if (burst_capture.status() == EdgeStatus::YELLOW) yellow++;
        
        if (expectancy.status() == EdgeStatus::RED) red++;
        else if (expectancy.status() == EdgeStatus::YELLOW) yellow++;
        
        if (red > 0) return EdgeStatus::RED;
        if (yellow >= 3) return EdgeStatus::YELLOW;
        return EdgeStatus::GREEN;
    }
    
    // === CONVENIENCE METHODS ===
    void onLatencyUpdate(double our_ms, double peer_ms = 0.0) {
        latency.update(our_ms, peer_ms);
    }
    
    void onScratch(bool would_have_lost, double potential_loss_bps = 0.0) {
        scratch.recordScratch(would_have_lost, potential_loss_bps);
    }
    
    void onEntry(int64_t our_ns, int64_t market_median_ns) {
        early_entry.recordEntry(our_ns, market_median_ns);
    }
    
    void onBurstExit(double captured_bps, double total_burst_bps) {
        burst_capture.recordBurst(captured_bps, total_burst_bps);
    }
    
    void onTradeComplete(double pnl_bps, bool is_fast_mode) {
        if (is_fast_mode) {
            expectancy.recordFastTrade(pnl_bps);
        } else {
            expectancy.recordSlowTrade(pnl_bps);
        }
    }
    
    // === RESET ===
    void resetDaily() {
        scratch.reset();
        early_entry.reset();
        burst_capture.reset();
        expectancy.reset();
    }
    
    // === DIAGNOSTICS ===
    void print() const {
        printf("\n[SPEED-EDGE-METRICS] ═══════════════════════════════════════\n");
        latency.print();
        scratch.print();
        early_entry.print();
        burst_capture.print();
        expectancy.print();
        printf("[SPEED-EDGE-METRICS] AGGREGATE: %s\n", edge_status_str(aggregateStatus()));
        printf("[SPEED-EDGE-METRICS] ═══════════════════════════════════════\n\n");
    }
    
    // === JSON FOR DASHBOARD ===
    void toJSON(char* buf, size_t bufsize) const {
        snprintf(buf, bufsize,
            "{"
            "\"latency\":{\"our_ms\":%.2f,\"peer_ms\":%.2f,\"edge_ms\":%.2f,\"status\":\"%s\"},"
            "\"scratch\":{\"total\":%u,\"saved\":%u,\"rate\":%.3f,\"pnl_saved\":%.2f,\"status\":\"%s\"},"
            "\"early_entry\":{\"avg_delta_ms\":%.2f,\"entries\":%u,\"status\":\"%s\"},"
            "\"burst_capture\":{\"captured_bps\":%.2f,\"total_bps\":%.2f,\"rate\":%.3f,\"count\":%u,\"status\":\"%s\"},"
            "\"expectancy\":{\"fast_ev\":%.3f,\"slow_ev\":%.3f,\"delta\":%.3f,\"status\":\"%s\"},"
            "\"aggregate\":\"%s\""
            "}",
            latency.our_latency_ms, latency.peer_median_ms, latency.edge(), edge_status_str(latency.status()),
            scratch.scratches_total.load(), scratch.scratches_saved.load(), scratch.saveRate(), scratch.saved_pnl_bps.load(), edge_status_str(scratch.status()),
            early_entry.avgDeltaMs(), early_entry.entry_count.load(), edge_status_str(early_entry.status()),
            burst_capture.total_captured_bps.load(), burst_capture.total_burst_bps.load(), burst_capture.captureRate(), burst_capture.burst_count.load(), edge_status_str(burst_capture.status()),
            expectancy.fastEV(), expectancy.slowEV(), expectancy.evDelta(), edge_status_str(expectancy.status()),
            edge_status_str(aggregateStatus())
        );
    }
    
private:
    SpeedEdgeMetrics() = default;
};

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

inline bool isSpeedEdgePositive() {
    return SpeedEdgeMetrics::instance().isSpeedEdgePositive();
}

inline EdgeStatus getSpeedEdgeStatus() {
    return SpeedEdgeMetrics::instance().aggregateStatus();
}

inline void recordScratchSaved(bool would_have_lost, double potential_loss_bps = 0.0) {
    SpeedEdgeMetrics::instance().onScratch(would_have_lost, potential_loss_bps);
}

inline void recordEntryTiming(int64_t our_ns, int64_t market_median_ns) {
    SpeedEdgeMetrics::instance().onEntry(our_ns, market_median_ns);
}

inline void recordBurstCapture(double captured_bps, double total_burst_bps) {
    SpeedEdgeMetrics::instance().onBurstExit(captured_bps, total_burst_bps);
}

inline void recordTradeForEV(double pnl_bps, bool is_fast_mode) {
    SpeedEdgeMetrics::instance().onTradeComplete(pnl_bps, is_fast_mode);
}

} // namespace Speed
} // namespace Chimera
