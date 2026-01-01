#pragma once
// =============================================================================
// CryptoSafetyGovernor.hpp - Institutional-Grade Self-Regulation
// =============================================================================
// v4.9.8: AUTO-RELAX / AUTO-TIGHTEN + HARD BOUNDS ENFORCEMENT
//
// CORE PRINCIPLE:
//   You do NOT loosen risk when losing.
//   You only relax entry selectivity when:
//     - Edge exists
//     - Execution is healthy
//     - But filters are blocking frequency
//   And you tighten IMMEDIATELY when execution or fees degrade.
//
// GOVERNOR PRIORITY (evaluated in order):
//   1. Hard Bounds (ABSOLUTE - nothing crosses these)
//   2. Fee Dominance Governor (kills silent bleed)
//   3. Negative Drift Governor (anti-chop)
//   4. Over-Relax Governor (prevents filter collapse)
//   5. Auto-Relax / Auto-Tighten
//   6. Recovery Governor
//
// If a higher governor fires → lower logic is IGNORED.
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <atomic>

namespace Chimera {
namespace Crypto {

// =============================================================================
// Governor State
// =============================================================================
enum class GovernorState : uint8_t {
    NORMAL            = 0,  // Normal operation
    CLAMPED_FEE       = 1,  // Fee dominance clamp active
    CLAMPED_DRIFT     = 2,  // Negative drift clamp active
    RELAX_CAP_REACHED = 3   // Max relax steps hit
};

inline const char* governorStateStr(GovernorState s) {
    switch (s) {
        case GovernorState::NORMAL:            return "NORMAL";
        case GovernorState::CLAMPED_FEE:       return "CLAMPED_FEE";
        case GovernorState::CLAMPED_DRIFT:     return "CLAMPED_DRIFT";
        case GovernorState::RELAX_CAP_REACHED: return "RELAX_CAP";
    }
    return "UNK";
}

// =============================================================================
// Hard Bounds (IMMUTABLE - NEVER CROSS THESE)
// =============================================================================
struct HardBounds {
    double min_confidence;
    double max_confidence;
    double min_expectancy_bps;
    double max_expectancy_bps;
    int min_confirmation_ticks;
    int max_confirmation_ticks;
    int min_maker_timeout_ms;
    int max_maker_timeout_ms;
};

// Per-symbol hard bounds (EXACT from spec)
constexpr HardBounds BTC_BOUNDS {
    0.55, 0.72,    // confidence range
    0.20, 0.45,    // expectancy_bps range
    1, 3,          // confirmation_ticks range
    180, 420       // maker_timeout_ms range
};

constexpr HardBounds ETH_BOUNDS {
    0.58, 0.72,
    0.25, 0.55,
    1, 3,
    140, 300
};

constexpr HardBounds SOL_BOUNDS {
    0.56, 0.75,
    0.35, 0.80,
    1, 2,
    80, 200
};

// =============================================================================
// Symbol Parameters (what we're allowed to move)
// =============================================================================
struct SymbolParams {
    double entry_confidence_min;
    double expectancy_min_bps;
    int confirmation_ticks;
    int maker_timeout_ms;
    
    // v4.9.8: Additional dynamic parameters
    double gross_edge_min_bps;      // Entry gate (fee-agnostic)
    double net_exit_min_bps;        // Exit gate (fee-aware)
    int max_hold_ms;                // Maximum hold time
};

// =============================================================================
// Symbol Health Signals (rolling window metrics)
// =============================================================================
struct SymbolHealth {
    double gross_edge_rate;      // signals detected / minute
    double trade_rate;           // actual trades / minute
    double maker_fill_rate;      // maker fills / total fills
    double taker_ratio;          // taker fills / total fills
    double fee_dominance;        // fees_bps / gross_pnl_bps
    double net_expectancy_bps;   // rolling net expectancy
    int trades;                  // trades in window
    
    // v4.9.8: Additional metrics
    int fee_only_losses;         // losses where gross > 0 but net < 0
    int missed_moves;            // cancels followed by favorable move
};

// =============================================================================
// Relax State Tracker
// =============================================================================
struct RelaxState {
    int relax_steps = 0;
    int max_relax_steps = 3;
};

// =============================================================================
// Governor Telemetry (for dashboard visibility)
// =============================================================================
struct GovernorTelemetry {
    GovernorState state = GovernorState::NORMAL;
    bool forced_maker_only = false;
    int relax_steps = 0;
    double adjusted_confidence = 0.0;
    double adjusted_expectancy = 0.0;
    
    const char* stateStr() const { return governorStateStr(state); }
};

// =============================================================================
// CryptoSafetyGovernor - THE CORE
// =============================================================================
class CryptoSafetyGovernor {
public:
    explicit CryptoSafetyGovernor(const HardBounds& bounds, int max_relax)
        : bounds_(bounds) {
        relax_.max_relax_steps = max_relax;
    }
    
    // =========================================================================
    // MAIN ENTRY POINT - Call once per evaluation cycle
    // =========================================================================
    GovernorTelemetry step(SymbolParams& p, const SymbolHealth& h, bool allow_relax) {
        GovernorTelemetry t;
        
        // ALWAYS clamp to bounds first
        clampToBounds(p);
        
        // ─────────────────────────────────────────────────────────────────────
        // GOVERNOR 1: FEE DOMINANCE (highest priority)
        // ─────────────────────────────────────────────────────────────────────
        // If fees are eating >85% of gross PnL, we are bleeding silently
        if (h.trades >= 10 && h.fee_dominance > 0.85) {
            tightenFeeClamp(p);
            t.state = GovernorState::CLAMPED_FEE;
            t.forced_maker_only = true;
            relax_.relax_steps = 0;  // Reset relax counter
            updateTelemetry(t, p);
            return t;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // GOVERNOR 2: NEGATIVE DRIFT (anti-chop)
        // ─────────────────────────────────────────────────────────────────────
        // Edge exists but outcomes are consistently bad
        if (h.net_expectancy_bps < -0.05 && h.gross_edge_rate > 0.0) {
            tightenDriftClamp(p);
            t.state = GovernorState::CLAMPED_DRIFT;
            relax_.relax_steps = 0;
            updateTelemetry(t, p);
            return t;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // GOVERNOR 3: OVER-RELAX CAP
        // ─────────────────────────────────────────────────────────────────────
        // Prevent runaway relaxation
        if (relax_.relax_steps >= relax_.max_relax_steps) {
            t.state = GovernorState::RELAX_CAP_REACHED;
            updateTelemetry(t, p);
            return t;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // GOVERNOR 4: AUTO-RELAX
        // ─────────────────────────────────────────────────────────────────────
        // Trigger RELAX only if edge exists but trades are suppressed
        //
        // RELAX CONDITIONS (ALL must be true):
        //   - gross_edge_rate > 0 (edge exists)
        //   - trade_rate < gross_edge_rate * 0.5 (trades suppressed)
        //   - maker_fill_rate >= 0.55 (execution healthy)
        //   - taker_ratio <= 0.2 (not bleeding on fees)
        //   - fee_dominance < 0.7 (fees not dominant)
        if (allow_relax &&
            h.gross_edge_rate > 0.0 &&
            h.trade_rate < h.gross_edge_rate * 0.5 &&
            h.maker_fill_rate >= 0.55 &&
            h.taker_ratio <= 0.2 &&
            h.fee_dominance < 0.7) {
            
            relax(p);
            relax_.relax_steps++;
            t.state = GovernorState::NORMAL;
            updateTelemetry(t, p);
            return t;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // GOVERNOR 5: AUTO-TIGHTEN
        // ─────────────────────────────────────────────────────────────────────
        // Tighten whenever execution quality degrades
        //
        // TIGHTEN CONDITIONS (ANY true):
        //   - taker_ratio > 0.25
        //   - net_expectancy_bps < 0
        if (h.taker_ratio > 0.25 || h.net_expectancy_bps < 0.0) {
            tighten(p);
            relax_.relax_steps = 0;
            t.state = GovernorState::NORMAL;
            updateTelemetry(t, p);
            return t;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // GOVERNOR 6: RECOVERY
        // ─────────────────────────────────────────────────────────────────────
        // Check if we can recover from clamps
        if (prev_state_ != GovernorState::NORMAL && 
            h.maker_fill_rate >= 0.60 &&
            h.fee_dominance < 0.65 &&
            h.net_expectancy_bps > 0.0 &&
            h.trades >= 5) {
            
            recover(p);
            relax_.relax_steps = 0;
            t.state = GovernorState::NORMAL;
            updateTelemetry(t, p);
            return t;
        }
        
        // No action needed
        t.state = GovernorState::NORMAL;
        updateTelemetry(t, p);
        return t;
    }
    
    // =========================================================================
    // State Access
    // =========================================================================
    GovernorState currentState() const { return prev_state_; }
    int relaxSteps() const { return relax_.relax_steps; }
    const HardBounds& bounds() const { return bounds_; }
    
    // For persistence (state restore on startup)
    void setRelaxSteps(int steps) { 
        relax_.relax_steps = std::clamp(steps, 0, relax_.max_relax_steps); 
    }
    void setState(GovernorState s) { prev_state_ = s; }
    int maxRelaxSteps() const { return relax_.max_relax_steps; }
    
    // =========================================================================
    // Reset
    // =========================================================================
    void reset() {
        relax_.relax_steps = 0;
        prev_state_ = GovernorState::NORMAL;
    }

private:
    HardBounds bounds_;
    RelaxState relax_;
    GovernorState prev_state_ = GovernorState::NORMAL;
    
    // ─────────────────────────────────────────────────────────────────────────
    // CLAMP TO HARD BOUNDS (always enforced)
    // ─────────────────────────────────────────────────────────────────────────
    void clampToBounds(SymbolParams& p) {
        p.entry_confidence_min = std::clamp(p.entry_confidence_min,
            bounds_.min_confidence, bounds_.max_confidence);
        
        p.expectancy_min_bps = std::clamp(p.expectancy_min_bps,
            bounds_.min_expectancy_bps, bounds_.max_expectancy_bps);
        
        p.confirmation_ticks = std::clamp(p.confirmation_ticks,
            bounds_.min_confirmation_ticks, bounds_.max_confirmation_ticks);
        
        p.maker_timeout_ms = std::clamp(p.maker_timeout_ms,
            bounds_.min_maker_timeout_ms, bounds_.max_maker_timeout_ms);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // RELAX (stepwise, restore trade flow)
    // ─────────────────────────────────────────────────────────────────────────
    void relax(SymbolParams& p) {
        p.entry_confidence_min -= 0.02;
        p.expectancy_min_bps   -= 0.05;
        p.confirmation_ticks   -= 1;
        p.maker_timeout_ms     += 30;
        clampToBounds(p);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // TIGHTEN (standard - when execution degrades)
    // ─────────────────────────────────────────────────────────────────────────
    void tighten(SymbolParams& p) {
        p.entry_confidence_min += 0.03;
        p.expectancy_min_bps   += 0.08;
        p.confirmation_ticks   += 1;
        p.maker_timeout_ms     -= 40;
        clampToBounds(p);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // TIGHTEN FEE CLAMP (aggressive - fee dominance detected)
    // ─────────────────────────────────────────────────────────────────────────
    void tightenFeeClamp(SymbolParams& p) {
        p.entry_confidence_min += 0.05;
        p.expectancy_min_bps   += 0.12;
        p.confirmation_ticks   += 1;
        p.maker_timeout_ms     += 60;  // More time for maker fills
        clampToBounds(p);
        prev_state_ = GovernorState::CLAMPED_FEE;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // TIGHTEN DRIFT CLAMP (chop detected)
    // ─────────────────────────────────────────────────────────────────────────
    void tightenDriftClamp(SymbolParams& p) {
        p.entry_confidence_min += 0.04;
        p.expectancy_min_bps   += 0.10;
        p.confirmation_ticks   += 1;
        clampToBounds(p);
        prev_state_ = GovernorState::CLAMPED_DRIFT;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // RECOVERY (stepwise, slower than tightening)
    // ─────────────────────────────────────────────────────────────────────────
    void recover(SymbolParams& p) {
        p.entry_confidence_min -= 0.02;
        p.expectancy_min_bps   -= 0.05;
        p.confirmation_ticks   -= 1;
        clampToBounds(p);
        prev_state_ = GovernorState::NORMAL;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    void updateTelemetry(GovernorTelemetry& t, const SymbolParams& p) {
        t.relax_steps = relax_.relax_steps;
        t.adjusted_confidence = p.entry_confidence_min;
        t.adjusted_expectancy = p.expectancy_min_bps;
        prev_state_ = t.state;
    }
};

// =============================================================================
// GOVERNOR HEAT METRIC (One number, no thinking)
// =============================================================================
// Heat ∈ [0.0, 1.0]
//   0.0 = fully relaxed
//   1.0 = fully clamped
//
// Interpretation:
//   0.0–0.3 = relaxed / aggressive
//   0.3–0.6 = normal
//   0.6–0.8 = cautious
//   0.8–1.0 = clamped / survival
// =============================================================================

inline double computeGovernorHeat(const SymbolParams& p,
                                   const HardBounds& b,
                                   int relax_steps,
                                   int max_relax_steps) {
    // Confidence component: how far from minimum are we?
    double c = (b.max_confidence - b.min_confidence) > 0.0
        ? (p.entry_confidence_min - b.min_confidence) / (b.max_confidence - b.min_confidence)
        : 0.0;
    
    // Expectancy component
    double e = (b.max_expectancy_bps - b.min_expectancy_bps) > 0.0
        ? (p.expectancy_min_bps - b.min_expectancy_bps) / (b.max_expectancy_bps - b.min_expectancy_bps)
        : 0.0;
    
    // Relax steps component (inverted: more steps = less heat initially, but capped = high heat)
    double t = max_relax_steps > 0
        ? static_cast<double>(relax_steps) / static_cast<double>(max_relax_steps)
        : 0.0;
    
    return std::clamp((c + e + t) / 3.0, 0.0, 1.0);
}

// =============================================================================
// GOVERNOR HEAT → SIZE SCALER
// =============================================================================
// Core principle: Heat controls aggressiveness, not permissions.
//   Heat ↑ → size ↓
//   Heat ↓ → size ↑
//   Trades may still occur when hot, but small and survivable
//
// Why piecewise instead of linear?
//   - Linear scaling causes jitter
//   - This creates stable regimes
//   - Matches how humans actually manage risk
// =============================================================================

constexpr double MIN_SIZE_MULTIPLIER = 0.20;  // Never collapse to zero

inline double governorHeatToSizeMultiplier(double heat) {
    if (heat <= 0.3) {
        return 1.00;          // full size
    }
    if (heat <= 0.6) {
        return 0.75;          // mild caution
    }
    if (heat <= 0.8) {
        return 0.50;          // defensive
    }
    return 0.25;              // survival mode
}

// Symbol-specific caps (from reality)
struct SizeScalerConfig {
    double max_size_mult;
    double min_size_mult;
};

constexpr SizeScalerConfig BTC_SIZE_CONFIG = {1.0, 0.25};
constexpr SizeScalerConfig ETH_SIZE_CONFIG = {1.0, 0.30};
constexpr SizeScalerConfig SOL_SIZE_CONFIG = {0.8, 0.35};

inline double computeFinalSizeMultiplier(double heat, const SizeScalerConfig& cfg) {
    double heat_mult = std::max(governorHeatToSizeMultiplier(heat), MIN_SIZE_MULTIPLIER);
    return std::clamp(heat_mult, cfg.min_size_mult, cfg.max_size_mult);
}

// =============================================================================
// DECISION TRACE (One line per tick, answers "why didn't it trade?")
// =============================================================================
struct DecisionTrace {
    const char* symbol;
    GovernorState governor_state;
    double governor_heat;
    double size_multiplier;
    bool profile_allowed;
    bool exec_allowed;
    bool ml_allowed;
    bool gonogo_allowed;
    double entry_conf;
    double exp_bps;
    int confirm_ticks;
    int maker_timeout_ms;
    
    void log() const {
        printf("[TRACE] %s gov=%s heat=%.2f size_mult=%.2f "
               "prof=%d exec=%d ml=%d gng=%d "
               "conf=%.2f exp=%.2f ct=%d mt=%d\n",
               symbol, governorStateStr(governor_state), governor_heat, size_multiplier,
               profile_allowed ? 1 : 0, exec_allowed ? 1 : 0,
               ml_allowed ? 1 : 0, gonogo_allowed ? 1 : 0,
               entry_conf, exp_bps, confirm_ticks, maker_timeout_ms);
    }
};

// =============================================================================
// Factory Functions
// =============================================================================
inline CryptoSafetyGovernor createBTCGovernor() {
    return CryptoSafetyGovernor(BTC_BOUNDS, 3);  // max 3 relax steps
}

inline CryptoSafetyGovernor createETHGovernor() {
    return CryptoSafetyGovernor(ETH_BOUNDS, 3);
}

inline CryptoSafetyGovernor createSOLGovernor() {
    return CryptoSafetyGovernor(SOL_BOUNDS, 2);  // SOL more restrictive
}

} // namespace Crypto
} // namespace Chimera
