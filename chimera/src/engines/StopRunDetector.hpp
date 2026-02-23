#pragma once
#include <cstdint>

namespace chimera {

enum class StopRunState {
    NONE = 0,
    UP = 1,
    DOWN = 2
};

class StopRunDetector {
public:
    StopRunDetector();

    StopRunState update(double price, double spread, double depth_top, uint64_t ts_ns);
    StopRunState state() const;

private:
    double m_last_price;
    uint64_t m_last_ts;
    StopRunState m_state;
};

}
