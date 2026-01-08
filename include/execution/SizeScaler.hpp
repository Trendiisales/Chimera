// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/SizeScaler.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: VOLATILITY-AWARE SIZE SCALING
//
// PURPOSE: Press when it pays, shrink when it doesn't.
// Institutions scale SIZE, not frequency.
//
// INPUTS:
// - Normalized volatility (current / baseline)
// - Win rate (recent)
// - Latency stability
// - Drawdown state
// - Session
//
// OUTPUT:
// - Size multiplier [0.25, 2.0]
//
// PHILOSOPHY:
// - High vol + stable latency → INCREASE size (opportunity)
// - High vol + unstable latency → DECREASE size (risk)
// - Winning → slight increase
// - Drawdown → decrease
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Size Scaling Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct SizeScalerConfig {
    // Volatility scaling
    double vol_boost_threshold = 1.2;      // Vol > 1.2x baseline → boost
    double vol_boost_multiplier = 1.20;    // +20% size when vol high
    double vol_reduce_threshold = 0.5;     // Vol < 0.5x baseline → reduce
    double vol_reduce_multiplier = 0.80;   // -20% size when vol low
    
    // Win rate scaling
    double win_rate_boost_threshold = 0.55;  // Win rate > 55% → boost
    double win_rate_boost_multiplier = 1.10; // +10% size when winning
    double win_rate_reduce_threshold = 0.40; // Win rate < 40% → reduce
    double win_rate_reduce_multiplier = 0.75; // -25% size when losing
    
    // Latency stability
    double latency_unstable_multiplier = 0.70; // -30% when latency unstable
    
    // Drawdown scaling
    double drawdown_light_threshold = 1.0;    // 1% DD → light reduction
    double drawdown_light_multiplier = 0.90;  // -10%
    double drawdown_medium_threshold = 2.0;   // 2% DD → medium reduction
    double drawdown_medium_multiplier = 0.70; // -30%
    double drawdown_severe_threshold = 4.0;   // 4% DD → severe reduction
    double drawdown_severe_multiplier = 0.40; // -60%
    
    // Hard limits
    double min_multiplier = 0.25;
    double max_multiplier = 2.00;
};

// ─────────────────────────────────────────────────────────────────────────────
// Size Scaling Result
// ─────────────────────────────────────────────────────────────────────────────
struct SizeScaleResult {
    double multiplier = 1.0;
    const char* reason = "DEFAULT";
    
    // Contributing factors (for logging)
    double vol_factor = 1.0;
    double winrate_factor = 1.0;
    double latency_factor = 1.0;
    double drawdown_factor = 1.0;
    double session_factor = 1.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Compute Size Multiplier - THE MAIN FUNCTION
// ─────────────────────────────────────────────────────────────────────────────
inline SizeScaleResult computeSizeScale(
    double vol_normalized,       // current_vol / baseline_vol
    double win_rate,             // 0-1, recent win rate
    bool latency_stable,         // Is latency within normal bounds?
    double drawdown_pct,         // Current drawdown percentage
    double session_multiplier,   // From SessionWeights
    const SizeScalerConfig& cfg = SizeScalerConfig{}
) {
    SizeScaleResult result;
    double mult = 1.0;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Factor 1: Volatility
    // ─────────────────────────────────────────────────────────────────────────
    if (vol_normalized > cfg.vol_boost_threshold && latency_stable) {
        result.vol_factor = cfg.vol_boost_multiplier;
    } else if (vol_normalized < cfg.vol_reduce_threshold) {
        result.vol_factor = cfg.vol_reduce_multiplier;
    } else {
        result.vol_factor = 1.0;
    }
    mult *= result.vol_factor;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Factor 2: Win Rate
    // ─────────────────────────────────────────────────────────────────────────
    if (win_rate > cfg.win_rate_boost_threshold) {
        result.winrate_factor = cfg.win_rate_boost_multiplier;
    } else if (win_rate < cfg.win_rate_reduce_threshold) {
        result.winrate_factor = cfg.win_rate_reduce_multiplier;
    } else {
        result.winrate_factor = 1.0;
    }
    mult *= result.winrate_factor;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Factor 3: Latency Stability
    // ─────────────────────────────────────────────────────────────────────────
    if (!latency_stable) {
        result.latency_factor = cfg.latency_unstable_multiplier;
    } else {
        result.latency_factor = 1.0;
    }
    mult *= result.latency_factor;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Factor 4: Drawdown
    // ─────────────────────────────────────────────────────────────────────────
    if (drawdown_pct >= cfg.drawdown_severe_threshold) {
        result.drawdown_factor = cfg.drawdown_severe_multiplier;
    } else if (drawdown_pct >= cfg.drawdown_medium_threshold) {
        result.drawdown_factor = cfg.drawdown_medium_multiplier;
    } else if (drawdown_pct >= cfg.drawdown_light_threshold) {
        result.drawdown_factor = cfg.drawdown_light_multiplier;
    } else {
        result.drawdown_factor = 1.0;
    }
    mult *= result.drawdown_factor;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Factor 5: Session
    // ─────────────────────────────────────────────────────────────────────────
    result.session_factor = session_multiplier;
    mult *= result.session_factor;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Clamp to limits
    // ─────────────────────────────────────────────────────────────────────────
    result.multiplier = std::clamp(mult, cfg.min_multiplier, cfg.max_multiplier);
    
    // Determine primary reason
    if (result.drawdown_factor < 0.8) {
        result.reason = "DRAWDOWN";
    } else if (!latency_stable) {
        result.reason = "LATENCY_UNSTABLE";
    } else if (result.winrate_factor < 0.9) {
        result.reason = "WIN_RATE_LOW";
    } else if (result.vol_factor > 1.0) {
        result.reason = "VOL_OPPORTUNITY";
    } else if (result.vol_factor < 1.0) {
        result.reason = "VOL_LOW";
    } else {
        result.reason = "NORMAL";
    }
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple Size Scale (for quick calculations)
// ─────────────────────────────────────────────────────────────────────────────
inline double simpleSizeScale(
    double vol_normalized,
    double win_rate,
    bool latency_stable
) {
    double s = 1.0;
    
    // Volatility boost/reduce
    if (vol_normalized > 1.2) s *= 1.2;
    else if (vol_normalized < 0.5) s *= 0.8;
    
    // Win rate adjustment
    if (win_rate > 0.55) s *= 1.1;
    else if (win_rate < 0.40) s *= 0.75;
    
    // Latency stability
    if (!latency_stable) s *= 0.7;
    
    return std::clamp(s, 0.5, 1.5);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kelly-Based Size Scaling (for advanced users)
// ─────────────────────────────────────────────────────────────────────────────
inline double kellyFraction(double win_rate, double avg_win, double avg_loss) {
    // Kelly: f* = (p * W - (1-p)) / W
    // Where p = win probability, W = win/loss ratio
    if (avg_loss == 0 || win_rate <= 0 || win_rate >= 1) return 0.0;
    
    double W = avg_win / avg_loss;
    double kelly = (win_rate * W - (1.0 - win_rate)) / W;
    
    // Use fractional Kelly (more conservative)
    return std::clamp(kelly * 0.25, 0.0, 0.5);  // 25% Kelly, max 50% bet
}

} // namespace Execution
} // namespace Chimera
