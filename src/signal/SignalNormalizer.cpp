#include "signal/SignalNormalizer.hpp"
#include <algorithm>

using namespace Chimera;

SignalNormalizer::SignalNormalizer(double clip)
    : clip_(clip) {}

double SignalNormalizer::norm(double v) const {
    if (v > clip_) return 1.0;
    if (v < -clip_) return -1.0;
    return v / clip_;
}
