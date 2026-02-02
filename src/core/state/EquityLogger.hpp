#pragma once
#include <string>
#include <cstdint>

namespace chimera {

class PositionState;

class EquityLogger {
public:
    EquityLogger(const std::string& path,
                 PositionState& ps);

    void tick(uint64_t ts_ns);

private:
    std::string m_path;
    PositionState& m_positions;
    uint64_t m_last_ts;
};

}
