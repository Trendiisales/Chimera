#pragma once

#include <string>
#include <cstdint>

enum class ExitAction {
    NONE,
    TIGHTEN_STOP,
    FORCE_EXIT
};

struct ImpulseDecayDecision {
    ExitAction action;
    const char* reason;
};

class ImpulseDecayExit {
public:
    ImpulseDecayExit();

    ImpulseDecayDecision evaluate(
        const std::string& symbol,
        double entry_impulse,
        double current_impulse,
        double unrealized_pnl,
        uint64_t entry_ts_ms,
        uint64_t now_ts_ms
    ) const;

private:
    ImpulseDecayDecision eval_xau(
        double entry_impulse,
        double current_impulse,
        double unrealized_pnl,
        uint64_t age_ms
    ) const;

    ImpulseDecayDecision eval_xag(
        double entry_impulse,
        double current_impulse,
        double unrealized_pnl,
        uint64_t age_ms
    ) const;

private:
    // === SAFETY ===
    static constexpr uint64_t MIN_AGE_MS = 120;

    // === XAU ===
    static constexpr double XAU_DECAY_WARN = 0.55;
    static constexpr double XAU_DECAY_EXIT = 0.35;
    static constexpr double XAU_MIN_PNL_EXIT = -0.20;

    // === XAG ===
    static constexpr double XAG_DECAY_WARN = 0.50;
    static constexpr double XAG_DECAY_EXIT = 0.30;
    static constexpr double XAG_MIN_PNL_EXIT = -0.10;
};
