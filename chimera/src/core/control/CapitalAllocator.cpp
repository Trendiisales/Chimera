#include "CapitalAllocator.hpp"
#include <algorithm>

namespace chimera {

CapitalAllocator::CapitalAllocator(double max_usd)
    : m_max_usd(max_usd) {}

double CapitalAllocator::allocate(double edge, double volatility, LatencyState state) {
    if (state == LatencyState::KILL) return 0.0;

    double size = m_max_usd * std::max(0.0, edge);
    size /= std::max(0.0001, volatility);

    if (state == LatencyState::DEGRADED) {
        size *= 0.1;
    }

    if (size > m_max_usd) size = m_max_usd;
    if (size < 0.0) size = 0.0;
    return size;
}

}
