#pragma once
#include <string>

namespace ChimeraV2 {

struct V2Config {
    // Capital discipline
    static constexpr double SHADOW_CAPITAL = 10000.0;
    static constexpr double DAILY_MAX_LOSS = 200.0;
    static constexpr double LOT_SIZE = 0.001;

    // V1 PROVEN: Concurrency limits (TUNED +100%)
    static constexpr int MAX_CONCURRENT_TOTAL = 6;      // Was 3, now 6
    static constexpr int MAX_CONCURRENT_PER_SYMBOL = 3; // Was 2, now 3
    static constexpr int XAU_MAX_OPEN_LEGS = 3;         // Was 2, now 3
    static constexpr int XAG_MAX_OPEN_LEGS = 3;         // Was 2, now 3

    // V1 PROVEN: Timing limits (TUNED)
    static constexpr int MAX_HOLD_SECONDS = 150;        // Was 120, now 150
    static constexpr uint64_t MIN_AGE_MS = 120;
    static constexpr int XAU_COOLDOWN_MS = 560;         // Was 800, reduced 30%
    static constexpr int XAG_COOLDOWN_MS = 560;         // Was 800, reduced 30%

    // V1 PROVEN: Stop distances
    static constexpr double XAU_STOP_DISTANCE = 2.20;  // From V1: $2.20 = R
    static constexpr double XAG_STOP_DISTANCE = 0.15;  // From V1: $0.15 = R

    // V1 PROVEN: Target at 2R
    static constexpr double TARGET_R_MULTIPLE = 2.0;

    // V1 PROVEN: ATR-based position sizing
    static constexpr double ATR_PER_LEG = 2.5;
    static constexpr int MAX_LEGS_HARD = 3;
    static constexpr bool ASIA_ONE_LEG_ONLY = true;

    // V1 PROVEN: Structural thresholds
    static constexpr double MOMENTUM_SPREAD_MULTIPLIER = 0.25;  // 25% of spread
    static constexpr double MIN_DRIFT_VELOCITY = 0.015;
    static constexpr double MAX_DRIFT_VELOCITY = 0.12;
    static constexpr double MAX_DRIFT_SPREAD = 0.30;

    // V1 PROVEN: Impulse thresholds (TUNED -15%)
    static constexpr double XAU_MIN_IMPULSE_FAST = 0.119;  // Was 0.14, now 0.119 (-15%)
    static constexpr double XAU_MIN_IMPULSE_OPEN = 0.085;  // Was 0.10, now 0.085 (-15%)
    static constexpr double XAG_MIN_IMPULSE_FAST = 0.068;  // Was 0.08, now 0.068 (-15%)

    // V1 PROVEN: Impulse decay exit
    static constexpr double XAU_DECAY_WARN = 0.48;  // Tighten stop
    static constexpr double XAU_DECAY_EXIT = 0.30;  // Force exit
    static constexpr double XAU_MIN_PNL_EXIT = -0.20;
    static constexpr double XAG_DECAY_WARN = 0.50;
    static constexpr double XAG_DECAY_EXIT = 0.30;
    static constexpr double XAG_MIN_PNL_EXIT = -0.10;

    // V1 PROVEN: Latency gates
    static constexpr double XAU_MAX_P95_FAST = 5.0;   // milliseconds
    static constexpr double XAG_MAX_P95_FAST = 6.5;
    static constexpr int XAU_ENTRY_RTT_MAX_MS = 8;    // p95 hard gate
    static constexpr int XAG_ENTRY_RTT_MAX_MS = 12;

    // V1 PROVEN: Loss blocking
    static constexpr int XAU_BLOCK_ON_LOSS_COUNT = 2;  // ONE bad exit = pause
    static constexpr int ENGINE_MAX_CONSEC_LOSSES = 3;
    static constexpr int PORTFOLIO_MAX_CONSEC_LOSSES = 5;

    // V1 PROVEN: Cooldown periods
    static constexpr int ENGINE_COOLDOWN_SECONDS = 600;
    static constexpr int PORTFOLIO_COOLDOWN_SECONDS = 300;
    
    // V2 additions (not in V1)
    static constexpr int REENTRY_GUARD_SECONDS = 5;
    static constexpr int WEAK_TRADE_EXIT_SECONDS = 60;
    static constexpr double WEAK_TRADE_R_THRESHOLD = 0.5;

    // V1 PROVEN: Session windows (UTC)
    static constexpr int LONDON_OPEN_START = 7;
    static constexpr int LONDON_OPEN_END = 10;
    static constexpr int NY_OPEN_START = 12;
    static constexpr int NY_OPEN_END = 16;
};

}
