#include "LiquidityVacuum.hpp"

namespace chimera {

LiquidityVacuum::LiquidityVacuum()
    : m_last_depth(0.0), m_last_ts(0), m_state(VacuumState::STABLE) {}

VacuumState LiquidityVacuum::update(double depth_top, uint64_t ts_ns) {
    if (m_last_ts == 0) {
        m_last_depth = depth_top;
        m_last_ts = ts_ns;
        return m_state;
    }

    double decay = m_last_depth - depth_top;
    if (decay > 0.5) {
        m_state = VacuumState::VACUUM;
    } else {
        m_state = VacuumState::STABLE;
    }

    m_last_depth = depth_top;
    m_last_ts = ts_ns;
    return m_state;
}

VacuumState LiquidityVacuum::state() const {
    return m_state;
}

}
