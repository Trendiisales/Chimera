#pragma once

// ============================================================================
// DRIFT / ABSORPTION ENTRY PARAMETERS
// Monetizes pre-impulse microstructure during FAST latency regimes
// ============================================================================

namespace DriftConfig {

// === XAUUSD DRIFT PARAMETERS ===
struct XAU {
    // Velocity band (below impulse threshold)
    static constexpr double DRIFT_VEL_MIN = 0.015;  // Minimum structured drift
    static constexpr double DRIFT_VEL_MAX = 0.12;   // Maximum before impulse
    
    // Position sizing (conservative)
    static constexpr double DRIFT_SIZE_MULT = 0.45; // 45% of base size
    
    // Exit targets (tight, mean-revert assumption)
    static constexpr double DRIFT_TP_USD = 0.55;    // $0.55 take profit
    static constexpr double DRIFT_SL_USD = 0.35;    // $0.35 stop loss
    
    // Spread constraint
    static constexpr double DRIFT_MAX_SPREAD = 0.30;
};

// === XAGUSD DRIFT PARAMETERS ===
struct XAG {
    // Velocity band
    static constexpr double DRIFT_VEL_MIN = 0.004;  // Smaller moves in silver
    static constexpr double DRIFT_VEL_MAX = 0.025;
    
    // Position sizing
    static constexpr double DRIFT_SIZE_MULT = 0.50; // 50% of base size
    
    // Exit targets
    static constexpr double DRIFT_TP_USD = 0.08;    // $0.08 take profit
    static constexpr double DRIFT_SL_USD = 0.05;    // $0.05 stop loss
    
    // Spread constraint
    static constexpr double DRIFT_MAX_SPREAD = 0.06;
};

// === EXPOSURE LIMITS (HARD CAPS) ===
static constexpr double DRIFT_MAX_USD_EXPOSURE = 1.20;   // Max drift exposure
static constexpr double IMPULSE_MAX_USD_EXPOSURE = 3.00; // Max impulse exposure

// === DRIFT KILL-SWITCH CONDITIONS ===
struct KillSwitch {
    static constexpr double PNL_LAST_20_MIN = -2.0;    // Disable if losing $2 over 20 trades
    static constexpr double WIN_RATE_MIN = 0.55;       // Disable if win rate < 55%
    static constexpr double LATENCY_P95_MAX = 7.0;     // Disable if latency degrades
    static constexpr int SPREAD_VIOLATION_MS = 500;    // Disable if spread wide >500ms
};

// === ADAPTIVE FREEZE PARAMETERS ===
struct Freeze {
    static constexpr uint64_t BASE_FREEZE_MS = 250;      // Base freeze duration
    static constexpr uint64_t DRIFT_FREEZE_MS = 120;     // Shorter freeze for drift
    static constexpr double VELOCITY_IMPROVEMENT_CANCEL = 1.15; // Cancel freeze if vel improves 15%
};

} // namespace DriftConfig
