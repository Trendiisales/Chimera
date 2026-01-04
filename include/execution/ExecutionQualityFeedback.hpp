// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ExecutionQualityFeedback.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: EXECUTION QUALITY FEEDBACK LOOP
//
// PURPOSE: Close the loop from fills → thresholds → execution mode.
// Without this, systems lie to themselves.
//
// TRACKS:
// - Fill rate by execution mode
// - Reject rate by symbol
// - Slippage by time of day
// - Latency percentiles
//
// ACTIONS:
// - Switch execution mode when evidence accumulates
// - Tighten thresholds when rejects spike
// - Adjust edge pricing based on measured slippage
//
// PERSISTENCE:
// - Save to runtime/profiles/SYMBOL.json
// - Reload on startup
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Execution Statistics Per Symbol
// ─────────────────────────────────────────────────────────────────────────────
struct ExecutionStats {
    // Order counts
    uint64_t orders_sent = 0;
    uint64_t orders_acked = 0;
    uint64_t orders_filled = 0;
    uint64_t orders_rejected = 0;
    uint64_t orders_cancelled = 0;
    
    // Fill breakdown
    uint64_t maker_fills = 0;
    uint64_t taker_fills = 0;
    
    // Timing
    uint64_t last_update_ns = 0;
    
    // Derived metrics
    double rejectRate() const {
        return orders_sent > 0 ? static_cast<double>(orders_rejected) / orders_sent : 0.0;
    }
    
    double fillRate() const {
        return orders_acked > 0 ? static_cast<double>(orders_filled) / orders_acked : 0.0;
    }
    
    double makerFillRate() const {
        uint64_t total_fills = maker_fills + taker_fills;
        return total_fills > 0 ? static_cast<double>(maker_fills) / total_fills : 0.0;
    }
    
    void reset() {
        orders_sent = orders_acked = orders_filled = orders_rejected = orders_cancelled = 0;
        maker_fills = taker_fills = 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Execution Profile
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolExecutionProfile {
    char symbol[16] = {0};
    
    // Fees (in bps)
    double maker_fee_bps = 2.0;
    double taker_fee_bps = 5.0;
    
    // Measured slippage (in bps)
    double avg_slippage_bps = 0.5;
    double slippage_p95_bps = 2.0;
    
    // Latency percentiles (in ms)
    double ack_p50_ms = 0.5;
    double ack_p80_ms = 1.0;
    double ack_p95_ms = 2.0;
    
    // Cancel latency
    double cancel_p95_ms = 2.5;
    
    // Reject rate
    double reject_rate = 0.05;
    
    // Computed minimum edge
    double min_edge_bps = 0.0;
    double latency_penalty_bps_per_ms = 0.5;
    
    // Execution mode
    bool taker_only = false;
    bool maker_primary = true;
    
    // Last update
    uint64_t last_updated_ns = 0;
    
    // Compute minimum edge based on profile
    void computeMinEdge() {
        min_edge_bps = taker_fee_bps +
                       avg_slippage_bps +
                       (ack_p95_ms * latency_penalty_bps_per_ms);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Feedback Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct FeedbackConfig {
    // Thresholds for switching to TAKER
    double reject_rate_switch_to_taker = 0.15;
    double fill_rate_switch_to_taker = 0.20;
    
    // Thresholds for switching back to MAKER
    double reject_rate_allow_maker = 0.05;
    int min_fills_for_maker = 20;
    
    // Edge adjustment
    double min_edge_multiplier_on_reject = 1.20;
};

// ─────────────────────────────────────────────────────────────────────────────
// Apply Feedback - Update Profile Based on Stats
// ─────────────────────────────────────────────────────────────────────────────
inline void applyFeedback(
    SymbolExecutionProfile& profile,
    const ExecutionStats& stats,
    const FeedbackConfig& cfg = FeedbackConfig{}
) {
    double reject_rate = stats.rejectRate();
    double fill_rate = stats.fillRate();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 1: High reject rate → switch to TAKER + raise edge
    // ─────────────────────────────────────────────────────────────────────────
    if (reject_rate > cfg.reject_rate_switch_to_taker) {
        profile.taker_only = true;
        profile.maker_primary = false;
        profile.min_edge_bps *= cfg.min_edge_multiplier_on_reject;
        profile.reject_rate = reject_rate;  // Update tracked rate
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 2: Low fill rate on maker → switch to TAKER
    // ─────────────────────────────────────────────────────────────────────────
    if (fill_rate < cfg.fill_rate_switch_to_taker && stats.orders_filled > 10) {
        profile.taker_only = true;
        profile.maker_primary = false;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 3: Good stats → allow MAKER again
    // ─────────────────────────────────────────────────────────────────────────
    if (reject_rate < cfg.reject_rate_allow_maker && 
        stats.orders_filled > static_cast<uint64_t>(cfg.min_fills_for_maker)) {
        profile.taker_only = false;
        profile.maker_primary = true;
    }
    
    // Update profile reject rate
    profile.reject_rate = reject_rate;
    profile.computeMinEdge();
}

// ─────────────────────────────────────────────────────────────────────────────
// Record Fill for Feedback
// ─────────────────────────────────────────────────────────────────────────────
inline void recordFill(ExecutionStats& stats, bool is_maker) {
    stats.orders_filled++;
    if (is_maker) {
        stats.maker_fills++;
    } else {
        stats.taker_fills++;
    }
}

inline void recordAck(ExecutionStats& stats) {
    stats.orders_acked++;
}

inline void recordReject(ExecutionStats& stats) {
    stats.orders_rejected++;
}

inline void recordSent(ExecutionStats& stats) {
    stats.orders_sent++;
}

inline void recordCancel(ExecutionStats& stats) {
    stats.orders_cancelled++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update Latency Percentiles (rolling window)
// ─────────────────────────────────────────────────────────────────────────────
class LatencyTracker {
public:
    static constexpr size_t MAX_SAMPLES = 1000;
    
    void record(double latency_ms) {
        if (samples_.size() >= MAX_SAMPLES) {
            samples_.erase(samples_.begin());
        }
        samples_.push_back(latency_ms);
        dirty_ = true;
    }
    
    double p50() {
        computeIfDirty();
        return p50_;
    }
    
    double p80() {
        computeIfDirty();
        return p80_;
    }
    
    double p95() {
        computeIfDirty();
        return p95_;
    }
    
    size_t count() const { return samples_.size(); }
    
private:
    std::vector<double> samples_;
    double p50_ = 0.0, p80_ = 0.0, p95_ = 0.0;
    bool dirty_ = true;
    
    void computeIfDirty() {
        if (!dirty_ || samples_.empty()) return;
        
        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        
        size_t n = sorted.size();
        p50_ = sorted[n * 50 / 100];
        p80_ = sorted[n * 80 / 100];
        p95_ = sorted[std::min(n * 95 / 100, n - 1)];
        
        dirty_ = false;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Profile Builder (from measured data)
// ─────────────────────────────────────────────────────────────────────────────
inline SymbolExecutionProfile buildProfile(
    const char* symbol,
    double fee_bps,
    double slippage_bps,
    double ack_p95_ms,
    double reject_rate,
    bool is_crypto
) {
    SymbolExecutionProfile p;
    strncpy(p.symbol, symbol, 15);
    p.symbol[15] = '\0';
    
    p.taker_fee_bps = fee_bps;
    p.maker_fee_bps = fee_bps * 0.4;  // Typical maker rebate
    p.avg_slippage_bps = slippage_bps;
    p.slippage_p95_bps = slippage_bps * 3.0;
    
    p.ack_p95_ms = ack_p95_ms;
    p.ack_p80_ms = ack_p95_ms * 0.6;
    p.ack_p50_ms = ack_p95_ms * 0.4;
    
    p.latency_penalty_bps_per_ms = is_crypto ? 0.6 : 0.25;
    p.reject_rate = reject_rate;
    
    p.taker_only = is_crypto;
    p.maker_primary = !is_crypto;
    
    p.computeMinEdge();
    
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Default Profiles for Known Symbols
// ─────────────────────────────────────────────────────────────────────────────
inline SymbolExecutionProfile getDefaultProfile(const char* symbol) {
    // BTCUSDT
    if (strcmp(symbol, "BTCUSDT") == 0) {
        return buildProfile("BTCUSDT", 5.0, 1.0, 2.0, 0.05, true);
    }
    // ETHUSDT
    if (strcmp(symbol, "ETHUSDT") == 0) {
        return buildProfile("ETHUSDT", 5.0, 1.5, 2.0, 0.05, true);
    }
    // SOLUSDT
    if (strcmp(symbol, "SOLUSDT") == 0) {
        return buildProfile("SOLUSDT", 5.0, 2.0, 2.5, 0.06, true);
    }
    // XAUUSD
    if (strcmp(symbol, "XAUUSD") == 0) {
        return buildProfile("XAUUSD", 3.0, 0.8, 5.0, 0.03, false);
    }
    // XAGUSD
    if (strcmp(symbol, "XAGUSD") == 0) {
        return buildProfile("XAGUSD", 3.5, 1.2, 5.5, 0.04, false);
    }
    // NAS100
    if (strcmp(symbol, "NAS100") == 0) {
        return buildProfile("NAS100", 2.5, 0.5, 8.0, 0.02, false);
    }
    // US30
    if (strcmp(symbol, "US30") == 0) {
        return buildProfile("US30", 2.5, 0.6, 8.0, 0.02, false);
    }
    
    // Default
    return buildProfile(symbol, 4.0, 1.0, 3.0, 0.05, false);
}

} // namespace Execution
} // namespace Chimera
