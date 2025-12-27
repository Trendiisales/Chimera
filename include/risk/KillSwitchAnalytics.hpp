#pragma once
// =============================================================================
// KillSwitchAnalytics.hpp v4.2.2 - Kill-Switch Dashboard Metrics
// =============================================================================
// Required metrics for tuning safety:
//   - Kill reason histogram
//   - Time-to-recover per event
//   - Per-symbol kill frequency
//   - Latency vs kill correlation
//   - PnL saved by kills
// =============================================================================

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include "KillSwitchLadder.hpp"

namespace Omega {

// =============================================================================
// KILL REASON HISTOGRAM
// =============================================================================
struct KillReasonHistogram {
    std::atomic<uint64_t> latency_critical{0};
    std::atomic<uint64_t> latency_freeze{0};
    std::atomic<uint64_t> latency_throttle{0};
    std::atomic<uint64_t> latency_warn{0};
    
    std::atomic<uint64_t> slippage_critical{0};
    std::atomic<uint64_t> slippage_freeze{0};
    std::atomic<uint64_t> slippage_throttle{0};
    std::atomic<uint64_t> slippage_warn{0};
    
    std::atomic<uint64_t> drawdown_critical{0};
    std::atomic<uint64_t> drawdown_freeze{0};
    
    std::atomic<uint64_t> error_critical{0};
    std::atomic<uint64_t> error_freeze{0};
    
    std::atomic<uint64_t> consec_loss_freeze{0};
    std::atomic<uint64_t> consec_loss_throttle{0};
    
    void recordKill(const char* reason) {
        if (!reason || reason[0] == '\0') return;
        
        // Parse reason and increment appropriate counter
        if (strstr(reason, "LATENCY")) {
            if (strstr(reason, "CRITICAL")) latency_critical.fetch_add(1);
            else if (strstr(reason, "FREEZE")) latency_freeze.fetch_add(1);
            else if (strstr(reason, "THROTTLE")) latency_throttle.fetch_add(1);
            else latency_warn.fetch_add(1);
        }
        else if (strstr(reason, "SLIPPAGE")) {
            if (strstr(reason, "CRITICAL")) slippage_critical.fetch_add(1);
            else if (strstr(reason, "FREEZE")) slippage_freeze.fetch_add(1);
            else if (strstr(reason, "THROTTLE")) slippage_throttle.fetch_add(1);
            else slippage_warn.fetch_add(1);
        }
        else if (strstr(reason, "DRAWDOWN")) {
            if (strstr(reason, "CRITICAL")) drawdown_critical.fetch_add(1);
            else drawdown_freeze.fetch_add(1);
        }
        else if (strstr(reason, "ERROR")) {
            if (strstr(reason, "CRITICAL")) error_critical.fetch_add(1);
            else error_freeze.fetch_add(1);
        }
        else if (strstr(reason, "CONSEC_LOSS")) {
            if (strstr(reason, "FREEZE")) consec_loss_freeze.fetch_add(1);
            else consec_loss_throttle.fetch_add(1);
        }
    }
    
    uint64_t total() const {
        return latency_critical.load() + latency_freeze.load() + 
               latency_throttle.load() + latency_warn.load() +
               slippage_critical.load() + slippage_freeze.load() +
               slippage_throttle.load() + slippage_warn.load() +
               drawdown_critical.load() + drawdown_freeze.load() +
               error_critical.load() + error_freeze.load() +
               consec_loss_freeze.load() + consec_loss_throttle.load();
    }
};

// =============================================================================
// RECOVERY TIME TRACKER - Per-event time to recover
// =============================================================================
struct RecoveryTimeTracker {
    std::atomic<uint64_t> total_recovery_ns{0};
    std::atomic<uint64_t> recovery_count{0};
    std::atomic<uint64_t> max_recovery_ns{0};
    std::atomic<uint64_t> last_halt_ns{0};
    
    void recordHalt(uint64_t now_ns) {
        last_halt_ns.store(now_ns, std::memory_order_relaxed);
    }
    
    void recordRecovery(uint64_t now_ns) {
        uint64_t halt_time = last_halt_ns.load(std::memory_order_relaxed);
        if (halt_time > 0 && now_ns > halt_time) {
            uint64_t recovery_time = now_ns - halt_time;
            total_recovery_ns.fetch_add(recovery_time, std::memory_order_relaxed);
            recovery_count.fetch_add(1, std::memory_order_relaxed);
            
            // Track max
            uint64_t current_max = max_recovery_ns.load(std::memory_order_relaxed);
            while (recovery_time > current_max) {
                if (max_recovery_ns.compare_exchange_weak(current_max, recovery_time)) break;
            }
        }
        last_halt_ns.store(0, std::memory_order_relaxed);
    }
    
    double avgRecoveryMs() const {
        uint64_t count = recovery_count.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return (total_recovery_ns.load() / count) / 1'000'000.0;
    }
    
    double maxRecoveryMs() const {
        return max_recovery_ns.load(std::memory_order_relaxed) / 1'000'000.0;
    }
};

// =============================================================================
// PNL SAVED TRACKER - Estimate of losses prevented
// =============================================================================
struct PnLSavedTracker {
    std::atomic<int64_t> estimated_pnl_saved_millibps{0};
    std::atomic<uint64_t> blocks_counted{0};
    
    // Called when a trade is blocked by kill-switch
    // edge_bps = expected edge of blocked trade
    // adverse_rate = probability trade would have gone wrong
    void recordBlock(double edge_bps, double adverse_selection_rate) {
        // Estimate: blocked edge × adverse rate = saved loss
        double saved = edge_bps * adverse_selection_rate * 2.0;  // 2× for potential loss
        estimated_pnl_saved_millibps.fetch_add(
            static_cast<int64_t>(saved * 1000), 
            std::memory_order_relaxed
        );
        blocks_counted.fetch_add(1, std::memory_order_relaxed);
    }
    
    double totalSavedBps() const {
        return estimated_pnl_saved_millibps.load() / 1000.0;
    }
};

// =============================================================================
// PER-SYMBOL KILL FREQUENCY
// =============================================================================
struct SymbolKillFrequency {
    char symbol[16] = {0};
    std::atomic<uint64_t> kills{0};
    std::atomic<uint64_t> last_kill_ns{0};
    
    void record(uint64_t now_ns) {
        kills.fetch_add(1, std::memory_order_relaxed);
        last_kill_ns.store(now_ns, std::memory_order_relaxed);
    }
};

// =============================================================================
// LATENCY vs KILL CORRELATION
// =============================================================================
struct LatencyKillCorrelation {
    // Buckets: 0-5ms, 5-10ms, 10-15ms, 15-20ms, 20-30ms, 30ms+
    static constexpr size_t BUCKET_COUNT = 6;
    std::array<std::atomic<uint64_t>, BUCKET_COUNT> latency_bucket_kills{};
    std::array<std::atomic<uint64_t>, BUCKET_COUNT> latency_bucket_samples{};
    
    size_t getBucket(double latency_ms) const {
        if (latency_ms < 5.0) return 0;
        if (latency_ms < 10.0) return 1;
        if (latency_ms < 15.0) return 2;
        if (latency_ms < 20.0) return 3;
        if (latency_ms < 30.0) return 4;
        return 5;
    }
    
    void recordSample(double latency_ms) {
        size_t bucket = getBucket(latency_ms);
        latency_bucket_samples[bucket].fetch_add(1, std::memory_order_relaxed);
    }
    
    void recordKill(double latency_ms) {
        size_t bucket = getBucket(latency_ms);
        latency_bucket_kills[bucket].fetch_add(1, std::memory_order_relaxed);
    }
    
    double killRate(size_t bucket) const {
        uint64_t samples = latency_bucket_samples[bucket].load();
        if (samples == 0) return 0.0;
        return static_cast<double>(latency_bucket_kills[bucket].load()) / samples;
    }
};

// =============================================================================
// COMPLETE KILL-SWITCH ANALYTICS
// =============================================================================
class KillSwitchAnalytics {
public:
    KillReasonHistogram reason_histogram;
    RecoveryTimeTracker recovery_tracker;
    PnLSavedTracker pnl_saved;
    LatencyKillCorrelation latency_correlation;
    
    static constexpr size_t MAX_SYMBOLS = 30;
    std::array<SymbolKillFrequency, MAX_SYMBOLS> symbol_frequency;
    size_t symbol_count = 0;
    
    void recordKill(const char* symbol, const char* reason, double latency_ms, uint64_t now_ns) {
        reason_histogram.recordKill(reason);
        recovery_tracker.recordHalt(now_ns);
        latency_correlation.recordKill(latency_ms);
        
        // Find or create symbol entry
        for (size_t i = 0; i < symbol_count; i++) {
            if (strcmp(symbol_frequency[i].symbol, symbol) == 0) {
                symbol_frequency[i].record(now_ns);
                return;
            }
        }
        if (symbol_count < MAX_SYMBOLS) {
            strncpy(symbol_frequency[symbol_count].symbol, symbol, 15);
            symbol_frequency[symbol_count].record(now_ns);
            symbol_count++;
        }
    }
    
    void recordRecovery(uint64_t now_ns) {
        recovery_tracker.recordRecovery(now_ns);
    }
    
    void recordLatencySample(double latency_ms) {
        latency_correlation.recordSample(latency_ms);
    }
    
    void recordBlockedTrade(double edge_bps, double adverse_rate) {
        pnl_saved.recordBlock(edge_bps, adverse_rate);
    }
    
    std::string renderJson() const {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2);
        
        out << "{\n";
        out << "  \"kill_reason_histogram\": {\n";
        out << "    \"latency_critical\": " << reason_histogram.latency_critical.load() << ",\n";
        out << "    \"latency_freeze\": " << reason_histogram.latency_freeze.load() << ",\n";
        out << "    \"latency_throttle\": " << reason_histogram.latency_throttle.load() << ",\n";
        out << "    \"slippage_critical\": " << reason_histogram.slippage_critical.load() << ",\n";
        out << "    \"slippage_freeze\": " << reason_histogram.slippage_freeze.load() << ",\n";
        out << "    \"drawdown_critical\": " << reason_histogram.drawdown_critical.load() << ",\n";
        out << "    \"total\": " << reason_histogram.total() << "\n";
        out << "  },\n";
        
        out << "  \"recovery\": {\n";
        out << "    \"avg_recovery_ms\": " << recovery_tracker.avgRecoveryMs() << ",\n";
        out << "    \"max_recovery_ms\": " << recovery_tracker.maxRecoveryMs() << ",\n";
        out << "    \"recovery_count\": " << recovery_tracker.recovery_count.load() << "\n";
        out << "  },\n";
        
        out << "  \"pnl_saved_bps\": " << pnl_saved.totalSavedBps() << ",\n";
        out << "  \"blocks_counted\": " << pnl_saved.blocks_counted.load() << ",\n";
        
        out << "  \"latency_correlation\": [\n";
        for (size_t i = 0; i < LatencyKillCorrelation::BUCKET_COUNT; i++) {
            out << "    { \"bucket\": " << i << ", \"kill_rate\": " 
                << latency_correlation.killRate(i) << " }";
            if (i < LatencyKillCorrelation::BUCKET_COUNT - 1) out << ",";
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        
        return out.str();
    }
};

// Global analytics singleton
inline KillSwitchAnalytics& GetKillSwitchAnalytics() {
    static KillSwitchAnalytics analytics;
    return analytics;
}

} // namespace Omega
