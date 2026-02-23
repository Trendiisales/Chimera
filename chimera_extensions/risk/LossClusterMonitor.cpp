#include "LossClusterMonitor.hpp"
#include <iostream>

namespace chimera {
namespace risk {

LossClusterMonitor::LossClusterMonitor() 
    : m_cooldown_start(std::chrono::steady_clock::now()) {}

void LossClusterMonitor::record_trade(bool win) {
    if (win) {
        m_consecutive_losses.store(0);
    } else {
        int losses = m_consecutive_losses.fetch_add(1) + 1;
        
        // GAP #7 FIX: Activate cooldown after cluster
        if (losses >= LOSS_THRESHOLD && !m_cooldown_active.load()) {
            m_cooldown_active.store(true);
            m_cooldown_start = std::chrono::steady_clock::now();
            
            std::cerr << "⚠️ LOSS CLUSTER DETECTED - " 
                      << losses << " consecutive losses\n"
                      << "   Entering " << COOLDOWN_SECONDS << "s cooldown\n";
        }
    }
}

bool LossClusterMonitor::is_cooldown_active() const {
    if (!m_cooldown_active.load())
        return false;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_cooldown_start).count();
    
    if (elapsed >= COOLDOWN_SECONDS) {
        // Cooldown expired
        return false;
    }
    
    return true;
}

void LossClusterMonitor::reset_cooldown() {
    m_cooldown_active.store(false);
    m_consecutive_losses.store(0);
}

} // namespace risk
} // namespace chimera
