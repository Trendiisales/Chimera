#include "execution/ExecutionGovernor.h"
#include <cmath>

static constexpr double IMPULSE_STRONG = 0.30;
static constexpr double IMPULSE_WEAK   = 0.18;

// Drift / absorption thresholds (derived from your logs)
static constexpr double DRIFT_MIN_VEL   = 0.015;
static constexpr double DRIFT_MAX_VEL   = 0.12;
static constexpr double DRIFT_MAX_SPREAD = 0.30;

// Sizing
static constexpr double SIZE_STRONG = 1.20;
static constexpr double SIZE_WEAK   = 1.00;
static constexpr double SIZE_DRIFT  = 0.55;

// TP multipliers
static constexpr double TP_FAST     = 1.35;
static constexpr double TP_DRIFT    = 0.60;

// Freeze logic
static constexpr uint64_t BASE_FREEZE_NS  = 250'000'000;
static constexpr uint64_t DRIFT_FREEZE_NS = 120'000'000;

static uint64_t entry_freeze_until = 0;

EntryDecision decide_entry(
    double velocity,
    double spread,
    const LatencyStats& latency,
    uint64_t now_ns
) {
    LatencyRegime lr = classify_latency(latency);
    double abs_vel = std::abs(velocity);

    // Hard freeze
    if (now_ns < entry_freeze_until) {
        return { false, EntryClass::NONE, 0.0, 0.0, "ENTRY_FREEZE" };
    }

    // Strong impulse
    if (abs_vel >= IMPULSE_STRONG) {
        return {
            true,
            EntryClass::STRONG_IMPULSE,
            SIZE_STRONG,
            (lr == LatencyRegime::FAST ? TP_FAST : 1.0),
            "STRONG_IMPULSE"
        };
    }

    // Weak impulse
    if (abs_vel >= IMPULSE_WEAK) {
        return {
            true,
            EntryClass::WEAK_IMPULSE,
            SIZE_WEAK,
            (lr == LatencyRegime::FAST ? TP_FAST : 1.0),
            "WEAK_IMPULSE"
        };
    }

    // 🚀 Drift / absorption entry (FAST only)
    if (lr == LatencyRegime::FAST &&
        abs_vel >= DRIFT_MIN_VEL &&
        abs_vel <= DRIFT_MAX_VEL &&
        spread <= DRIFT_MAX_SPREAD
    ) {
        entry_freeze_until = now_ns + DRIFT_FREEZE_NS;
        return {
            true,
            EntryClass::DRIFT,
            SIZE_DRIFT,
            TP_DRIFT,
            "DRIFT_ENTRY"
        };
    }

    // Adaptive freeze
    entry_freeze_until = now_ns + BASE_FREEZE_NS;
    return { false, EntryClass::NONE, 0.0, 0.0, "NO_EDGE" };
}
