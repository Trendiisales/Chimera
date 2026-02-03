#include "control/PositionGate.hpp"
#include <iostream>
#include <cmath>

namespace chimera {

PositionGate::PositionGate() {
    for (int i = 0; i < 3; ++i)
        m_blocked_until[i].store(0, std::memory_order_relaxed);
}

void PositionGate::configure(const PositionGateConfig& cfg) {
    m_cfg = cfg;
    std::cout << "[POSITION_GATE] Config: cap=" << m_cfg.max_pos_per_symbol 
              << " suppress=" << m_cfg.suppress_ms << "ms\n";
}

bool PositionGate::allow(int sid,
                          double current_pos,
                          const std::string& strat,
                          const std::string& symbol,
                          uint64_t now_ns) {
    if (sid < 0 || sid >= 3) return false;
    
    uint64_t blocked = m_blocked_until[sid].load(std::memory_order_relaxed);
    if (now_ns < blocked)
        return false;

    if (std::abs(current_pos) >= m_cfg.max_pos_per_symbol) {
        uint64_t suppress_ns = m_cfg.suppress_ms * 1'000'000ULL;
        uint64_t until = now_ns + suppress_ns;
        m_blocked_until[sid].store(until, std::memory_order_relaxed);

        std::cout << "[POSITION_GATE] BLOCK "
                  << strat << " "
                  << symbol
                  << " pos=" << current_pos
                  << " cap=" << m_cfg.max_pos_per_symbol
                  << " suppress=" << m_cfg.suppress_ms << "ms\n";

        return false;
    }

    return true;
}

uint64_t PositionGate::blocked_until(int sid) const {
    if (sid < 0 || sid >= 3) return 0;
    return m_blocked_until[sid].load(std::memory_order_relaxed);
}

}
