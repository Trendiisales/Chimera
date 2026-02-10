#include "risk/ImpulseDecayExit.hpp"

ImpulseDecayExit::ImpulseDecayExit() {}

ImpulseDecayDecision ImpulseDecayExit::evaluate(
    const std::string& symbol,
    double entry_impulse,
    double current_impulse,
    double unrealized_pnl,
    uint64_t entry_ts_ms,
    uint64_t now_ts_ms
) const {
    if (entry_impulse <= 0.0) {
        return { ExitAction::NONE, "NO_ENTRY_IMPULSE" };
    }

    uint64_t age_ms = now_ts_ms - entry_ts_ms;
    if (age_ms < MIN_AGE_MS) {
        return { ExitAction::NONE, "TOO_EARLY" };
    }

    if (symbol == "XAUUSD") {
        return eval_xau(entry_impulse, current_impulse, unrealized_pnl, age_ms);
    }
    if (symbol == "XAGUSD") {
        return eval_xag(entry_impulse, current_impulse, unrealized_pnl, age_ms);
    }

    return { ExitAction::NONE, "NO_SYMBOL_RULE" };
}

// =======================
// XAU (STRICT)
// =======================
ImpulseDecayDecision ImpulseDecayExit::eval_xau(
    double entry_impulse,
    double current_impulse,
    double unrealized_pnl,
    uint64_t age_ms
) const {
    double abs_entry = (entry_impulse < 0) ? -entry_impulse : entry_impulse;
    double abs_current = (current_impulse < 0) ? -current_impulse : current_impulse;
    double ratio = abs_current / abs_entry;

    if (ratio <= XAU_DECAY_EXIT && unrealized_pnl <= XAU_MIN_PNL_EXIT) {
        return { ExitAction::FORCE_EXIT, "XAU_IMPULSE_COLLAPSE" };
    }

    if (ratio <= XAU_DECAY_WARN) {
        return { ExitAction::TIGHTEN_STOP, "XAU_IMPULSE_DECAY" };
    }

    return { ExitAction::NONE, "XAU_OK" };
}

// =======================
// XAG (FLEXIBLE)
// =======================
ImpulseDecayDecision ImpulseDecayExit::eval_xag(
    double entry_impulse,
    double current_impulse,
    double unrealized_pnl,
    uint64_t age_ms
) const {
    double abs_entry = (entry_impulse < 0) ? -entry_impulse : entry_impulse;
    double abs_current = (current_impulse < 0) ? -current_impulse : current_impulse;
    double ratio = abs_current / abs_entry;

    if (ratio <= XAG_DECAY_EXIT && unrealized_pnl <= XAG_MIN_PNL_EXIT) {
        return { ExitAction::FORCE_EXIT, "XAG_IMPULSE_COLLAPSE" };
    }

    if (ratio <= XAG_DECAY_WARN) {
        return { ExitAction::TIGHTEN_STOP, "XAG_IMPULSE_DECAY" };
    }

    return { ExitAction::NONE, "XAG_OK" };
}
