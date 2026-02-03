#include "control/DriftFilter.hpp"
#include <cmath>

namespace chimera {

DriftFilter::DriftFilter()
    : m_mult(1.8) {}  // Only block trades when drift > 1.8x spread

void DriftFilter::set_multiplier(double m) {
    m_mult = m;
}

bool DriftFilter::is_trending(double mid_now,
                              double mid_prev,
                              double spread) const {
    double move = std::abs(mid_now - mid_prev);
    double threshold = spread * m_mult;
    return move > threshold;
}

}
