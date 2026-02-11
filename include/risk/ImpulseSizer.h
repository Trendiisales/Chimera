#pragma once

#include <string>

enum class LatencyRegime;

struct SizeDecision {
    double multiplier;
    const char* reason;
};

class ImpulseSizer {
public:
    ImpulseSizer();

    SizeDecision compute(
        const std::string& symbol,
        LatencyRegime latency_regime,
        double impulse_velocity,
        bool is_buy
    ) const;

private:
    SizeDecision size_xau(
        LatencyRegime latency_regime,
        double impulse_velocity,
        bool is_buy
    ) const;

    SizeDecision size_xag(
        LatencyRegime latency_regime,
        double impulse_velocity,
        bool is_buy
    ) const;

private:
    // === CAPS ===
    static constexpr double MAX_MULT = 1.20;

    // === XAU thresholds ===
    static constexpr double XAU_MED_IMPULSE    = 0.15;
    static constexpr double XAU_STRONG_IMPULSE = 0.26;

    // === XAG thresholds ===
    static constexpr double XAG_MED_IMPULSE = 0.10;
    static constexpr double XAG_STRONG_IMPULSE = 0.18;
};
