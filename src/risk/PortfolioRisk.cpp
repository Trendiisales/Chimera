#include "risk/PortfolioRisk.hpp"

using namespace Chimera;

PortfolioRisk::PortfolioRisk()
    : max_total_mult_(1.0) {}

RiskDecision PortfolioRisk::combine(
    const RiskDecision& strategy_risk,
    double arbiter_mult,
    bool venue_ok,
    uint64_t ts_ns
) {
    RiskDecision r{};
    r.ts_ns = ts_ns;

    if (!venue_ok || strategy_risk.verdict == RiskVerdict::BLOCK) {
        r.verdict = RiskVerdict::BLOCK;
        r.risk_multiplier = 0.0;
        return r;
    }

    double m = strategy_risk.risk_multiplier * arbiter_mult;
    if (m > max_total_mult_) m = max_total_mult_;

    r.verdict = RiskVerdict::ALLOW;
    r.risk_multiplier = m;
    return r;
}
