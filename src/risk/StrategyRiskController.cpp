#include "risk/StrategyRiskController.hpp"

using namespace Chimera;

StrategyRiskController::StrategyRiskController()
    : max_confidence_(1.0),
      vol_limit_(0.7),
      min_interval_ns_(500ULL * 1000 * 1000),
      last_action_ts_(0) {}

RiskDecision StrategyRiskController::assess(
    const StrategyDecision& decision,
    double volatility,
    bool kill_switch_active,
    uint64_t ts_ns
) {
    RiskDecision r{};
    r.ts_ns = ts_ns;
    r.verdict = RiskVerdict::BLOCK;
    r.risk_multiplier = 0.0;

    if (kill_switch_active) {
        return r;
    }

    if (volatility > vol_limit_) {
        return r;
    }

    if (last_action_ts_ != 0 && ts_ns - last_action_ts_ < min_interval_ns_) {
        return r;
    }

    if (decision.intent == StrategyIntent::FLAT) {
        r.verdict = RiskVerdict::ALLOW;
        r.risk_multiplier = 0.0;
        last_action_ts_ = ts_ns;
        return r;
    }

    double conf = decision.confidence;
    if (conf > max_confidence_) conf = max_confidence_;

    r.verdict = RiskVerdict::ALLOW;
    r.risk_multiplier = conf;
    last_action_ts_ = ts_ns;
    return r;
}
