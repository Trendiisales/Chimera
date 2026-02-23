#pragma once
#include <cstdint>

namespace chimera {

enum class VacuumState {
    STABLE = 0,
    VACUUM = 1
};

class LiquidityVacuum {
public:
    LiquidityVacuum();

    VacuumState update(double depth_top, uint64_t ts_ns);
    VacuumState state() const;

private:
    double m_last_depth;
    uint64_t m_last_ts;
    VacuumState m_state;
};

}
