#include "chimera/control/RegimeClassifier.hpp"

namespace chimera {

RegimeClassifier::RegimeClassifier()
    : regime_(Regime::BALANCED),
      quality_score_(1.0) {
}

void RegimeClassifier::update(double spread_bps,
                              double ofi_accel,
                              bool impulse_open,
                              int tick_rate) {
    // Penalize poor liquidity or unstable flow
    double score = 1.0;

    if (spread_bps > 15.0) score -= 0.3;
    if (tick_rate < 5)     score -= 0.3;
    if (impulse_open)     score -= 0.2;

    // Use OFI acceleration as flow stability signal
    if (ofi_accel < 0.0)  score -= 0.2;

    if (score <= 0.2) {
        regime_ = Regime::DEAD;
        quality_score_ = 0.0;
        return;
    }

    if (score <= 0.6) {
        regime_ = Regime::CHAOTIC;
        quality_score_ = score;
        return;
    }

    regime_ = Regime::BALANCED;
    quality_score_ = score;
}

RegimeClassifier::Regime RegimeClassifier::current() const {
    return regime_;
}

double RegimeClassifier::quality() const {
    return quality_score_;
}

} // namespace chimera
