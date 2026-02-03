#pragma once
#include <cstdint>
#include <atomic>
#include <string>

namespace chimera {

struct PositionGateConfig {
    double max_pos_per_symbol = 0.50;
    uint64_t suppress_ms = 75;
};

class PositionGate {
public:
    PositionGate();

    void configure(const PositionGateConfig& cfg);

    bool allow(int sid,
               double current_pos,
               const std::string& strat,
               const std::string& symbol,
               uint64_t now_ns);

    uint64_t blocked_until(int sid) const;

private:
    PositionGateConfig m_cfg;
    std::atomic<uint64_t> m_blocked_until[3];
};

}
