// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/MicroTrendPullbackAlpha.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.23: MICRO TREND PULLBACK (MTP)
// STATUS: 🔧 ACTIVE - BACKUP ALPHA
// OWNER: Jo
// CREATED: 2026-01-03
//
// THE BACKUP ALPHA (TREND regime only, institutional-safe)
//
// CORE IDEA:
//   You do NOT predict direction. You:
//   1. Confirm a real micro-trend
//   2. Wait for a pullback caused by liquidity refill
//   3. Enter with the dominant flow when liquidity thins again
//
// This is how most intraday desks actually trade.
//
// REGIME: TREND only (NOT STABLE, NOT VOLATILITY, NOT RANGE, NOT DEAD)
//
// SIGNAL STRUCTURE:
//   1. TREND QUALIFICATION (all must be true):
//      - OFI cumulative > threshold
//      - Price makes HH/HL (or LL/LH for down)
//      - Spread stays inside 70th percentile
//      - Volatility is stable or rising
//
//   2. PULLBACK DETECTION (valid pullback):
//      - Price retraces ≤ 38% of impulse
//      - Pullback volume decreases
//      - Order book shows liquidity refill, not aggression
//
//   3. ENTRY TRIGGER:
//      - Pullback stalls
//      - Liquidity thins on the trend side
//      - Aggressive orders resume in original direction
//
// EXIT:
//   - TP: 1.5–3.0 bps (small, fast)
//   - SL: hard, pre-defined
//   - TIME STOP: mandatory
//
// WHY THIS PRINTS:
//   - Trades participant behavior, not indicators
//   - Pullbacks are liquidity-driven, not reversal signals
//   - Trend continuation has structural follow-through
//   - Works without colo
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "alpha/MarketRegime.hpp"
#include "alpha/ExecutionAwareAlpha.hpp"

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// MTP Parameters
// ─────────────────────────────────────────────────────────────────────────────
struct MTPParams {
    // Trend qualification
    double min_ofi_cumulative = 0.40;       // Minimum cumulative OFI
    double max_spread_percentile = 0.70;    // Spread must be below this percentile
    double min_volatility_ratio = 0.80;     // Vol must be >= 80% of recent avg
    
    // Pullback detection
    double max_retracement_pct = 38.0;      // Max 38% retracement (Fib level)
    double pullback_volume_ratio = 0.60;    // Volume must drop to 60% of impulse
    double min_liquidity_refill = 1.20;     // Book depth must increase 20%
    
    // Entry trigger
    double resume_ofi_threshold = 0.20;     // OFI resumes at this level
    uint64_t min_stall_ns = 80'000'000ULL;  // 80ms stall before entry
    
    // Exit parameters  
    double tp_bps = 2.0;                    // Take profit
    double sl_bps = 1.5;                    // Stop loss
    uint64_t max_hold_ns = 350'000'000ULL;  // 350ms max hold
    
    // Execution
    bool taker_only = true;                 // NO MAKER
    bool kill_on_loss = true;
    bool allow_pyramiding = false;
    bool allow_reentry = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Market Snapshot for MTP
// ─────────────────────────────────────────────────────────────────────────────
struct MTPSnapshot {
    // Symbol
    const char* symbol = nullptr;
    uint16_t symbol_id = 0;
    
    // Regime (MUST be TREND)
    MarketRegime regime = MarketRegime::DEAD;
    
    // Trend state
    enum class TrendDir { NONE, UP, DOWN } trend_dir = TrendDir::NONE;
    double ofi_cumulative = 0.0;            // Cumulative OFI (signed)
    bool hh_hl_confirmed = false;           // Higher highs / higher lows (or inverse)
    
    // Price structure
    double last_impulse_high = 0.0;
    double last_impulse_low = 0.0;
    double current_price = 0.0;
    double impulse_size = 0.0;              // High - Low of impulse
    
    // Pullback state
    bool in_pullback = false;
    double retracement_pct = 0.0;           // Current retracement %
    double pullback_volume_ratio = 1.0;     // Pullback vol / impulse vol
    
    // Order book
    double bid = 0.0;
    double ask = 0.0;
    double spread_bps = 0.0;
    double spread_percentile = 0.5;         // Current spread vs rolling distribution
    double trend_side_depth = 0.0;          // Depth on trend side of book
    double trend_side_depth_prev = 0.0;     // Previous depth (for refill detection)
    
    // Volume / aggression
    double current_ofi = 0.0;               // Current tick OFI
    bool aggression_resumed = false;        // Aggressive flow in trend direction
    
    // Volatility
    double volatility_ratio = 1.0;          // Current vol / recent avg vol
    
    // Timing
    uint64_t pullback_start_ns = 0;
    uint64_t now_ns = 0;
    
    // Execution context
    double latency_p95_us = 0.0;
    double reject_rate = 0.0;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Derived calculations
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] double mid_price() const noexcept {
        return (bid + ask) / 2.0;
    }
    
    [[nodiscard]] bool liquidity_refilled() const noexcept {
        return trend_side_depth > trend_side_depth_prev * 1.20;  // 20% increase
    }
    
    [[nodiscard]] bool liquidity_thinning() const noexcept {
        return trend_side_depth < trend_side_depth_prev * 0.85;  // 15% decrease
    }
    
    [[nodiscard]] uint64_t stall_duration_ns() const noexcept {
        if (!in_pullback || pullback_start_ns == 0) return 0;
        return now_ns > pullback_start_ns ? now_ns - pullback_start_ns : 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Execution Intent for MTP
// ─────────────────────────────────────────────────────────────────────────────
struct MTPIntent {
    const char* alpha_name = "MicroTrendPullback";
    const char* symbol = nullptr;
    uint16_t symbol_id = 0;
    
    enum class Side { NONE, BUY, SELL } side = Side::NONE;
    
    double entry_price = 0.0;
    double size_scale = 1.0;
    
    double tp_bps = 2.0;
    double sl_bps = 1.5;
    uint64_t max_hold_ns = 350'000'000ULL;
    
    bool taker = true;
    bool kill_on_loss = true;
    bool allow_pyramiding = false;
    bool allow_reentry = false;
    
    // Diagnostics
    double raw_edge_bps = 0.0;
    double net_edge_bps = 0.0;
    double ofi_at_entry = 0.0;
    double retracement_at_entry = 0.0;
    uint64_t signal_ts_ns = 0;
    
    [[nodiscard]] bool valid() const noexcept {
        return side != Side::NONE && symbol != nullptr && entry_price > 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// MTP Alpha Implementation
// ─────────────────────────────────────────────────────────────────────────────
class MicroTrendPullbackAlpha {
public:
    explicit MicroTrendPullbackAlpha(const MTPParams& p = MTPParams{})
        : params_(p)
    {
        printf("[MTP] Alpha initialized: tp=%.1fbps sl=%.1fbps max_hold=%llums\n",
               params_.tp_bps, params_.sl_bps,
               static_cast<unsigned long long>(params_.max_hold_ns / 1'000'000));
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ENTRY DECISION
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool shouldEnter(const MTPSnapshot& s) const noexcept {
        // ─────────────────────────────────────────────────────────────────────
        // Rule 0: TREND regime only (HARD GATE)
        // ─────────────────────────────────────────────────────────────────────
        if (s.regime != MarketRegime::TREND) {
            return false;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 1: Trend must be qualified
        // ─────────────────────────────────────────────────────────────────────
        if (!trendQualified(s)) {
            return false;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 2: Valid pullback in progress
        // ─────────────────────────────────────────────────────────────────────
        if (!validPullback(s)) {
            return false;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 3: Entry trigger conditions
        // ─────────────────────────────────────────────────────────────────────
        if (!entryTriggered(s)) {
            return false;
        }
        
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // BUILD EXECUTION INTENT
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] MTPIntent buildIntent(const MTPSnapshot& s) const noexcept {
        MTPIntent intent;
        intent.alpha_name = name();
        intent.symbol = s.symbol;
        intent.symbol_id = s.symbol_id;
        
        // Direction based on trend
        intent.side = (s.trend_dir == MTPSnapshot::TrendDir::UP) 
                    ? MTPIntent::Side::BUY 
                    : MTPIntent::Side::SELL;
        
        intent.entry_price = s.mid_price();
        intent.taker = params_.taker_only;
        intent.size_scale = 1.0;
        
        intent.tp_bps = params_.tp_bps;
        intent.sl_bps = params_.sl_bps;
        intent.max_hold_ns = params_.max_hold_ns;
        
        intent.kill_on_loss = params_.kill_on_loss;
        intent.allow_pyramiding = params_.allow_pyramiding;
        intent.allow_reentry = params_.allow_reentry;
        
        // Diagnostics
        intent.ofi_at_entry = s.current_ofi;
        intent.retracement_at_entry = s.retracement_pct;
        intent.signal_ts_ns = s.now_ns;
        
        intent.raw_edge_bps = estimateEdge(s);
        intent.net_edge_bps = intent.raw_edge_bps - estimateExecutionCost(s);
        
        return intent;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ACCESSORS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] const char* name() const noexcept {
        return "MicroTrendPullback";
    }
    
    [[nodiscard]] const MTPParams& params() const noexcept {
        return params_;
    }
    
    MTPParams& params() noexcept {
        return params_;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DIAGNOSTICS
    // ═══════════════════════════════════════════════════════════════════════
    
    struct DiagnosticResult {
        bool regime_ok = false;
        bool trend_ok = false;
        bool pullback_ok = false;
        bool trigger_ok = false;
        bool all_passed = false;
        const char* rejection_reason = nullptr;
    };
    
    [[nodiscard]] DiagnosticResult diagnose(const MTPSnapshot& s) const noexcept {
        DiagnosticResult d;
        
        d.regime_ok = (s.regime == MarketRegime::TREND);
        if (!d.regime_ok) {
            d.rejection_reason = "REGIME_NOT_TREND";
            return d;
        }
        
        d.trend_ok = trendQualified(s);
        if (!d.trend_ok) {
            d.rejection_reason = "TREND_NOT_QUALIFIED";
            return d;
        }
        
        d.pullback_ok = validPullback(s);
        if (!d.pullback_ok) {
            d.rejection_reason = "INVALID_PULLBACK";
            return d;
        }
        
        d.trigger_ok = entryTriggered(s);
        if (!d.trigger_ok) {
            d.rejection_reason = "ENTRY_NOT_TRIGGERED";
            return d;
        }
        
        d.all_passed = true;
        d.rejection_reason = nullptr;
        return d;
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // INTERNAL CHECKS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool trendQualified(const MTPSnapshot& s) const noexcept {
        // Must have direction
        if (s.trend_dir == MTPSnapshot::TrendDir::NONE) {
            return false;
        }
        
        // OFI cumulative must be strong
        if (std::abs(s.ofi_cumulative) < params_.min_ofi_cumulative) {
            return false;
        }
        
        // OFI must match trend direction
        bool ofi_matches = (s.trend_dir == MTPSnapshot::TrendDir::UP && s.ofi_cumulative > 0) ||
                          (s.trend_dir == MTPSnapshot::TrendDir::DOWN && s.ofi_cumulative < 0);
        if (!ofi_matches) {
            return false;
        }
        
        // Price structure confirmed
        if (!s.hh_hl_confirmed) {
            return false;
        }
        
        // Spread within acceptable range
        if (s.spread_percentile > params_.max_spread_percentile) {
            return false;
        }
        
        // Volatility stable or rising
        if (s.volatility_ratio < params_.min_volatility_ratio) {
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] bool validPullback(const MTPSnapshot& s) const noexcept {
        // Must be in pullback state
        if (!s.in_pullback) {
            return false;
        }
        
        // Retracement within limits (max 38%)
        if (s.retracement_pct > params_.max_retracement_pct) {
            return false;
        }
        
        // Volume should decrease during pullback
        if (s.pullback_volume_ratio > params_.pullback_volume_ratio) {
            return false;
        }
        
        // Liquidity should have refilled (order book depth increased)
        if (!s.liquidity_refilled()) {
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] bool entryTriggered(const MTPSnapshot& s) const noexcept {
        // Pullback must have stalled for minimum duration
        if (s.stall_duration_ns() < params_.min_stall_ns) {
            return false;
        }
        
        // Liquidity must be thinning again (resumption starting)
        if (!s.liquidity_thinning()) {
            return false;
        }
        
        // Aggressive flow must resume in trend direction
        if (!s.aggression_resumed) {
            return false;
        }
        
        // OFI must show resumption
        bool ofi_resumed = (s.trend_dir == MTPSnapshot::TrendDir::UP && s.current_ofi > params_.resume_ofi_threshold) ||
                          (s.trend_dir == MTPSnapshot::TrendDir::DOWN && s.current_ofi < -params_.resume_ofi_threshold);
        if (!ofi_resumed) {
            return false;
        }
        
        return true;
    }
    
    [[nodiscard]] double estimateEdge(const MTPSnapshot& s) const noexcept {
        // Base edge from trend continuation probability
        double base_edge = 1.8;
        
        // Scale by trend strength
        double trend_factor = std::min(1.5, std::abs(s.ofi_cumulative) / params_.min_ofi_cumulative);
        
        // Better entries at smaller retracements
        double retrace_factor = 1.0 + (params_.max_retracement_pct - s.retracement_pct) / 100.0;
        
        return base_edge * trend_factor * retrace_factor * 0.5;
    }
    
    [[nodiscard]] double estimateExecutionCost(const MTPSnapshot& s) const noexcept {
        double cost = s.spread_bps;  // Taker crosses spread
        cost += s.latency_p95_us / 1000.0 * 0.05;  // Latency slippage
        cost += 5.0;  // Fees
        return cost;
    }
    
private:
    MTPParams params_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory for symbol-specific params
// ─────────────────────────────────────────────────────────────────────────────
inline MTPParams getMTPParams(const char* symbol) {
    MTPParams p;
    
    if (strstr(symbol, "BTC") != nullptr) {
        p.tp_bps = 1.8;
        p.sl_bps = 1.3;
    } else if (strstr(symbol, "ETH") != nullptr) {
        p.tp_bps = 2.2;
        p.sl_bps = 1.5;
    } else {
        p.tp_bps = 2.8;
        p.sl_bps = 1.8;
    }
    
    return p;
}

} // namespace Alpha
} // namespace Chimera
