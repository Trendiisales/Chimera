#include "CapitalAnomalyGuard.hpp"
#include <iostream>

namespace chimera {
namespace risk {

CapitalAnomalyGuard::CapitalAnomalyGuard(double global_cap)
    : m_global_cap(global_cap)
    , m_absolute_hard_limit(global_cap * 1.05) {}

void CapitalAnomalyGuard::check_and_enforce(double current_exposure,
                                           std::function<void()> emergency_shutdown) {
    // GAP #6 FIX: Hard kill if exposure exceeds 105% of cap
    if (current_exposure > m_absolute_hard_limit) {
        if (!m_emergency_active.load()) {
            m_emergency_active.store(true);
            
            std::cerr << "🔴 CAPITAL ANOMALY DETECTED\n"
                      << "   Exposure: $" << current_exposure << "\n"
                      << "   Hard Limit: $" << m_absolute_hard_limit << "\n"
                      << "   EMERGENCY SHUTDOWN TRIGGERED\n";
            
            if (emergency_shutdown) {
                emergency_shutdown();
            }
        }
    }
}

} // namespace risk
} // namespace chimera
