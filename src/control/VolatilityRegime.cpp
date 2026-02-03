#include "control/VolatilityRegime.hpp"
#include <cmath>

namespace chimera {

VolatilityRegime::VolatilityRegime()
    : m_idx(0),
      m_count(0),
      m_last_mid(0.0) {
    for (int i = 0; i < WINDOW; ++i)
        m_returns[i] = 0.0;
}

void VolatilityRegime::update(double mid_price) {
    if (m_last_mid > 0.0) {
        double ret = (mid_price - m_last_mid) / m_last_mid;
        m_returns[m_idx] = ret;
        m_idx = (m_idx + 1) % WINDOW;
        if (m_count < WINDOW)
            ++m_count;
    }
    m_last_mid = mid_price;
}

double VolatilityRegime::compute_sigma() const {
    if (m_count == 0)
        return 0.0;

    double mean = 0.0;
    for (int i = 0; i < m_count; ++i)
        mean += m_returns[i];
    mean /= m_count;

    double var = 0.0;
    for (int i = 0; i < m_count; ++i) {
        double d = m_returns[i] - mean;
        var += d * d;
    }
    var /= m_count;

    return std::sqrt(var);
}

double VolatilityRegime::sigma() const {
    return compute_sigma();
}

VolRegime VolatilityRegime::regime() const {
    double s = compute_sigma();

    // DISCOVERY MODE: Apply floor at 0.5 bps
    // Never let sigma collapse to zero in low-activity periods
    constexpr double MIN_SIGMA_BPS = 0.5;
    s = std::max(s, MIN_SIGMA_BPS / 10000.0);

    // Crypto spot has 0.01-0.1bp tick noise - treat as alive
    if (s < 0.0001)    // <1bp = normal microstructure
        return VolRegime::NORMAL;
    return VolRegime::EXPANSION;  // >1bp = volatility expansion
}

}
