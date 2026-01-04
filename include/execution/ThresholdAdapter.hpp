// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ThresholdAdapter.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: AUTO-RELAX / AUTO-TIGHTEN THRESHOLDS
//
// PURPOSE: Never starve, never overtrade.
// Institutions adapt thresholds based on market and system state.
//
// LOGIC:
// - No trades for X minutes + latency stable → RELAX (prevent starvation)
// - Drawdown increasing → TIGHTEN (protect capital)
// - High reject rate → TIGHTEN (venue problems)
// - Win streak → slight RELAX (momentum)
// - Loss streak → TIGHTEN (protect)
//
// APPLICATION:
// - Per-symbol threshold adjustment
// - Called once per minute (not per tick)
// - Multiplicative adjustment to min_edge_bps
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Threshold Adapter Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct ThresholdAdapterConfig {
    // Starvation prevention
    int minutes_no_trade_relax = 15;      // After 15 min no trades → relax
    double relax_factor = 0.90;           // Reduce threshold by 10%
    double max_relax_cumulative = 0.70;   // Never relax below 70% of original
    
    // Drawdown tightening
    double drawdown_light_tighten = 1.5;  // 1.5% DD → light tighten
    double tighten_factor_light = 1.10;   // +10%
    double drawdown_medium_tighten = 3.0; // 3% DD → medium tighten
    double tighten_factor_medium = 1.25;  // +25%
    double drawdown_severe_tighten = 5.0; // 5% DD → severe tighten
    double tighten_factor_severe = 1.50;  // +50%
    
    // Reject rate tightening
    double reject_rate_tighten = 0.10;    // >10% rejects → tighten
    double reject_tighten_factor = 1.15;  // +15%
    
    // Win/loss streak adjustment
    int win_streak_relax = 3;             // 3+ wins → slight relax
    double win_relax_factor = 0.95;       // -5%
    int loss_streak_tighten = 2;          // 2+ losses → tighten
    double loss_tighten_factor = 1.10;    // +10%
    
    // Hard limits
    double min_threshold_multiplier = 0.60;  // Never below 60% of baseline
    double max_threshold_multiplier = 2.00;  // Never above 200% of baseline
};

// ─────────────────────────────────────────────────────────────────────────────
// Threshold State Tracking
// ─────────────────────────────────────────────────────────────────────────────
struct ThresholdState {
    double current_multiplier = 1.0;
    double baseline_min_edge_bps = 0.0;  // Original threshold
    double effective_min_edge_bps = 0.0; // After adjustment
    
    int minutes_since_trade = 0;
    int consecutive_wins = 0;
    int consecutive_losses = 0;
    double current_drawdown_pct = 0.0;
    double recent_reject_rate = 0.0;
    
    const char* last_adjustment_reason = "NONE";
};

// ─────────────────────────────────────────────────────────────────────────────
// Adapt Thresholds - THE MAIN FUNCTION
// ─────────────────────────────────────────────────────────────────────────────
inline void adaptThresholds(
    ThresholdState& state,
    int minutes_since_trade,
    double drawdown_pct,
    double reject_rate,
    int consecutive_wins,
    int consecutive_losses,
    bool latency_stable,
    const ThresholdAdapterConfig& cfg = ThresholdAdapterConfig{}
) {
    double mult = 1.0;
    const char* reason = "NORMAL";
    
    // Update tracking
    state.minutes_since_trade = minutes_since_trade;
    state.current_drawdown_pct = drawdown_pct;
    state.recent_reject_rate = reject_rate;
    state.consecutive_wins = consecutive_wins;
    state.consecutive_losses = consecutive_losses;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 1: Starvation prevention (RELAX)
    // ─────────────────────────────────────────────────────────────────────────
    if (minutes_since_trade > cfg.minutes_no_trade_relax && latency_stable) {
        // Gradual relaxation: 10% per threshold crossing
        int relax_steps = (minutes_since_trade - cfg.minutes_no_trade_relax) / 10;
        double relax = std::pow(cfg.relax_factor, std::min(relax_steps + 1, 5));
        mult *= relax;
        reason = "STARVATION_RELAX";
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 2: Drawdown protection (TIGHTEN)
    // ─────────────────────────────────────────────────────────────────────────
    if (drawdown_pct >= cfg.drawdown_severe_tighten) {
        mult *= cfg.tighten_factor_severe;
        reason = "DRAWDOWN_SEVERE";
    } else if (drawdown_pct >= cfg.drawdown_medium_tighten) {
        mult *= cfg.tighten_factor_medium;
        reason = "DRAWDOWN_MEDIUM";
    } else if (drawdown_pct >= cfg.drawdown_light_tighten) {
        mult *= cfg.tighten_factor_light;
        reason = "DRAWDOWN_LIGHT";
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 3: Reject rate (TIGHTEN)
    // ─────────────────────────────────────────────────────────────────────────
    if (reject_rate > cfg.reject_rate_tighten) {
        mult *= cfg.reject_tighten_factor;
        if (strcmp(reason, "NORMAL") == 0) {
            reason = "HIGH_REJECTS";
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 4: Win streak (slight RELAX)
    // ─────────────────────────────────────────────────────────────────────────
    if (consecutive_wins >= cfg.win_streak_relax) {
        mult *= cfg.win_relax_factor;
        if (strcmp(reason, "NORMAL") == 0) {
            reason = "WIN_STREAK";
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Rule 5: Loss streak (TIGHTEN)
    // ─────────────────────────────────────────────────────────────────────────
    if (consecutive_losses >= cfg.loss_streak_tighten) {
        mult *= cfg.loss_tighten_factor;
        if (strcmp(reason, "NORMAL") == 0 || strcmp(reason, "WIN_STREAK") == 0) {
            reason = "LOSS_STREAK";
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Clamp and apply
    // ─────────────────────────────────────────────────────────────────────────
    state.current_multiplier = std::clamp(mult, 
                                          cfg.min_threshold_multiplier, 
                                          cfg.max_threshold_multiplier);
    state.effective_min_edge_bps = state.baseline_min_edge_bps * state.current_multiplier;
    state.last_adjustment_reason = reason;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize Threshold State
// ─────────────────────────────────────────────────────────────────────────────
inline ThresholdState initThresholdState(double baseline_min_edge_bps) {
    ThresholdState state;
    state.baseline_min_edge_bps = baseline_min_edge_bps;
    state.effective_min_edge_bps = baseline_min_edge_bps;
    state.current_multiplier = 1.0;
    return state;
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple Threshold Adaptation (for quick use)
// ─────────────────────────────────────────────────────────────────────────────
inline void simpleAdaptThreshold(
    double& min_edge_bps,
    int minutes_no_trade,
    double drawdown_pct,
    bool latency_stable
) {
    // Starvation relax
    if (minutes_no_trade > 20 && latency_stable) {
        min_edge_bps *= 0.90;  // -10%
    }
    
    // Drawdown tighten
    if (drawdown_pct > 2.0) {
        min_edge_bps *= 1.15;  // +15%
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset Thresholds (e.g., on new trading day)
// ─────────────────────────────────────────────────────────────────────────────
inline void resetThresholds(ThresholdState& state) {
    state.current_multiplier = 1.0;
    state.effective_min_edge_bps = state.baseline_min_edge_bps;
    state.minutes_since_trade = 0;
    state.consecutive_wins = 0;
    state.consecutive_losses = 0;
    state.last_adjustment_reason = "RESET";
}

} // namespace Execution
} // namespace Chimera
