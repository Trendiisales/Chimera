#include "chimera/infra/VenueBiasEngine.hpp"
#include <cmath>

namespace chimera {

void VenueBiasEngine::onFill(
    const std::string& venue,
    double expected,
    double fill,
    double fee_bps,
    double latency_ms
) {
    double slippage =
        std::abs(fill - expected);

    double penalty =
        slippage * 10000.0 +
        fee_bps +
        latency_ms * 0.01;

    VenueBias& b = map[venue];
    b.score =
        0.9 * b.score +
        0.1 * penalty;
}

double VenueBiasEngine::bias(
    const std::string& venue
) const {
    auto it = map.find(venue);
    if (it == map.end()) return 0.0;
    return it->second.score;
}

}
