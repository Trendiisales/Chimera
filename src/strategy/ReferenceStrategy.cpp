#include "strategy/ReferenceStrategy.hpp"
#include <cmath>

using namespace Chimera;

ReferenceStrategy::ReferenceStrategy()
    : last_intent_(StrategyIntent::FLAT),
      last_ts_(0),
      entry_threshold_(0.6),
      exit_threshold_(0.2),
      cooldown_ns_(500ULL * 1000 * 1000) {}

StrategyDecision ReferenceStrategy::on_signal(const AggregatedSignal& sig) {
    StrategyDecision d{};
    d.intent = last_intent_;
    d.confidence = std::abs(sig.composite);
    d.ts_ns = sig.ts_ns;

    if (last_ts_ != 0 && sig.ts_ns - last_ts_ < cooldown_ns_) {
        return d;
    }

    if (last_intent_ == StrategyIntent::FLAT) {
        if (sig.composite > entry_threshold_) {
            last_intent_ = StrategyIntent::LONG;
            last_ts_ = sig.ts_ns;
        } else if (sig.composite < -entry_threshold_) {
            last_intent_ = StrategyIntent::SHORT;
            last_ts_ = sig.ts_ns;
        }
    } else {
        if (std::abs(sig.composite) < exit_threshold_) {
            last_intent_ = StrategyIntent::FLAT;
            last_ts_ = sig.ts_ns;
        }
    }

    d.intent = last_intent_;
    return d;
}
