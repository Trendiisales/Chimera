#pragma once

#include <atomic>
#include <chrono>

namespace chimera {
namespace risk {

// GAP #7 FIX: Prevents spiral trading after loss clusters
class LossClusterMonitor {
public:
    LossClusterMonitor();
    
    void record_trade(bool win);
    bool is_cooldown_active() const;
    void reset_cooldown();

private:
    std::atomic<int> m_consecutive_losses{0};
    std::atomic<bool> m_cooldown_active{false};
    std::chrono::steady_clock::time_point m_cooldown_start;
    
    static constexpr int LOSS_THRESHOLD = 5;
    static constexpr int COOLDOWN_SECONDS = 60;
};

} // namespace risk
} // namespace chimera
