#include "risk/ImpulseSizer.h"
#include "risk/LatencyAwareTP.h"

ImpulseSizer::ImpulseSizer() {}

SizeDecision ImpulseSizer::compute(
    const std::string& symbol,
    LatencyRegime latency_regime,
    double impulse_velocity,
    bool is_buy
) const {
    if (symbol == "XAUUSD") {
        return size_xau(latency_regime, impulse_velocity, is_buy);
    }
    if (symbol == "XAGUSD") {
        return size_xag(latency_regime, impulse_velocity, is_buy);
    }

    return { 1.0, "NO_SYMBOL_OVERRIDE" };
}

// =======================
// XAU
// =======================
SizeDecision ImpulseSizer::size_xau(
    LatencyRegime latency_regime,
    double impulse_velocity,
    bool is_buy
) const {
    if (latency_regime != LatencyRegime::FAST) {
        return { 1.0, "XAU_NOT_FAST" };
    }

    if ((impulse_velocity > 0 && !is_buy) ||
        (impulse_velocity < 0 && is_buy)) {
        return { 1.0, "XAU_IMPULSE_DIRECTION_MISMATCH" };
    }

    double abs_vel = std::abs(impulse_velocity);

    if (abs_vel >= XAU_STRONG_IMPULSE) {
        return { 1.20, "XAU_STRONG_IMPULSE_FAST" };
    }

    if (abs_vel >= XAU_MED_IMPULSE) {
        return { 1.10, "XAU_MED_IMPULSE_FAST" };
    }

    return { 1.0, "XAU_WEAK_IMPULSE" };
}

// =======================
// XAG
// =======================
SizeDecision ImpulseSizer::size_xag(
    LatencyRegime latency_regime,
    double impulse_velocity,
    bool is_buy
) const {
    if (latency_regime != LatencyRegime::FAST) {
        return { 1.0, "XAG_NOT_FAST" };
    }

    if ((impulse_velocity > 0 && !is_buy) ||
        (impulse_velocity < 0 && is_buy)) {
        return { 1.0, "XAG_IMPULSE_DIRECTION_MISMATCH" };
    }

    double abs_vel = std::abs(impulse_velocity);

    if (abs_vel >= XAG_STRONG_IMPULSE) {
        return { 1.20, "XAG_STRONG_IMPULSE_FAST" };
    }

    if (abs_vel >= XAG_MED_IMPULSE) {
        return { 1.10, "XAG_MED_IMPULSE_FAST" };
    }

    return { 1.0, "XAG_WEAK_IMPULSE" };
}
