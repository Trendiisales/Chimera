#include "StopRunDetector.hpp"
#include <cmath>

namespace chimera {

StopRunDetector::StopRunDetector()
    : m_last_price(0.0), m_last_ts(0), m_state(StopRunState::NONE) {}

StopRunState StopRunDetector::update(double price, double spread, double depth_top, uint64_t ts_ns) {
    if (m_last_ts == 0) {
        m_last_price = price;
        m_last_ts = ts_ns;
        return m_state;
    }

    double dt_ms = double(ts_ns - m_last_ts) / 1e6;
    double dp = price - m_last_price;

    double velocity = 0.0;
    if (dt_ms > 0.0) velocity = dp / dt_ms;

    if (std::fabs(velocity) > 0.05 && spread > 0.02 && depth_top < 1.0) {
        m_state = (velocity > 0.0) ? StopRunState::UP : StopRunState::DOWN;
    } else {
        m_state = StopRunState::NONE;
    }

    m_last_price = price;
    m_last_ts = ts_ns;
    return m_state;
}

StopRunState StopRunDetector::state() const {
    return m_state;
}

}
