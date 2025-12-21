#include "regime/RegimeClassifier.hpp"
#include <cmath>

using namespace Chimera;

RegimeClassifier::RegimeClassifier()
    : vol_low_(0.2),
      vol_high_(0.6),
      trend_thresh_(0.5),
      mean_rev_thresh_(0.15),
      last_composite_(0.0) {}

RegimeState RegimeClassifier::on_signal(const AggregatedSignal& sig) {
    RegimeState r{};
    r.ts_ns = sig.ts_ns;

    // ---- Volatility regime ----
    if (sig.volatility < vol_low_) {
        r.vol = VolatilityRegime::LOW;
    } else if (sig.volatility > vol_high_) {
        r.vol = VolatilityRegime::HIGH;
    } else {
        r.vol = VolatilityRegime::NORMAL;
    }

    // ---- Behaviour regime ----
    double delta = sig.composite - last_composite_;
    last_composite_ = sig.composite;

    if (std::abs(sig.composite) > trend_thresh_ && std::abs(delta) < mean_rev_thresh_) {
        r.behaviour = BehaviourRegime::TRENDING;
        r.confidence = std::min(1.0, std::abs(sig.composite));
    } else if (std::abs(sig.composite) < mean_rev_thresh_) {
        r.behaviour = BehaviourRegime::MEAN_REVERTING;
        r.confidence = 1.0 - std::abs(sig.composite);
    } else {
        r.behaviour = BehaviourRegime::NOISY;
        r.confidence = 0.5;
    }

    return r;
}
