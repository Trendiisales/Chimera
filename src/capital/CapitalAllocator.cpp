#include "capital/CapitalAllocator.hpp"

using namespace Chimera;

CapitalAllocator::CapitalAllocator(double aum)
    : aum_(aum),
      max_fraction_(0.02),
      drawdown_limit_(0.15),
      vol_scale_low_(1.2),
      vol_scale_normal_(1.0),
      vol_scale_high_(0.4) {}

CapitalDecision CapitalAllocator::allocate(
    const RiskDecision& risk,
    const RegimeState& regime,
    double current_drawdown,
    uint64_t ts_ns
) {
    CapitalDecision c{};
    c.ts_ns = ts_ns;
    c.allow = false;
    c.notional_mult = 0.0;

    if (risk.verdict == RiskVerdict::BLOCK) {
        return c;
    }

    if (current_drawdown > drawdown_limit_) {
        return c;
    }

    double vol_scale = 1.0;
    switch (regime.vol) {
        case VolatilityRegime::LOW:
            vol_scale = vol_scale_low_;
            break;
        case VolatilityRegime::NORMAL:
            vol_scale = vol_scale_normal_;
            break;
        case VolatilityRegime::HIGH:
            vol_scale = vol_scale_high_;
            break;
    }

    double base = aum_ * max_fraction_;
    double final_mult = base * risk.risk_multiplier * vol_scale;

    c.allow = true;
    c.notional_mult = final_mult;
    return c;
}
