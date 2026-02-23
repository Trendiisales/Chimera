#pragma once

#include <atomic>
#include <functional>

namespace chimera {
namespace risk {

// GAP #6 FIX: Hard floor prevents allocator runaway
class CapitalAnomalyGuard {
public:
    explicit CapitalAnomalyGuard(double global_cap);
    
    void check_and_enforce(double current_exposure,
                          std::function<void()> emergency_shutdown);
    
    bool is_emergency_active() const { return m_emergency_active.load(); }

private:
    double m_global_cap;
    double m_absolute_hard_limit;  // 105% of cap
    std::atomic<bool> m_emergency_active{false};
};

} // namespace risk
} // namespace chimera
