#include "chimera/control/RegimeClassifier.hpp"

namespace chimera {

RegimeClassifier::RegimeClassifier()
    : regime_(Regime::BALANCED),
      quality_score_(0.5) {}

void RegimeClassifier::update(
    double spread_bps,
    double ofi_accel,
    bool impulse_open,
    int tick_rate
) {
    // BALANCED: Normal market conditions
    // CHAOTIC: High volatility, wide spreads
    // DEAD: Low liquidity, slow ticks
    
    if (spread_bps > 10.0 || ofi_accel > 2.0) {
        regime_ = Regime::CHAOTIC;
        quality_score_ = 0.3;  // Lower quality
    } else if (tick_rate < 5 && spread_bps > 5.0) {
        regime_ = Regime::DEAD;
        quality_score_ = 0.1;  // Very low quality
    } else {
        regime_ = Regime::BALANCED;
        quality_score_ = 0.8;  // Good quality
    }
}

RegimeClassifier::Regime RegimeClassifier::current() const {
    return regime_;
}

double RegimeClassifier::quality() const {
    return quality_score_;
}

}
