#include "control/UnwindCoordinator.hpp"
#include <iostream>

namespace chimera {

void UnwindCoordinator::try_lock(const std::string& symbol, 
                                  const std::string& engine_id,
                                  double position) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    double abs_pos = std::fabs(position);
    
    // If already at or above cap and not yet locked, lock it
    if (abs_pos >= MAX_POSITION) {
        auto it = locks_.find(symbol);
        if (it == locks_.end() || !it->second.locked) {
            // Lock this symbol, attributing to this engine
            locks_[symbol] = LockState{
                true,
                engine_id,
                position
            };
            std::cout << "[UNWIND] LOCK " << symbol << " by " << engine_id 
                      << " pos=" << position << "\n";
        }
    }
}

bool UnwindCoordinator::can_trade(const std::string& symbol, 
                                   const std::string& engine_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = locks_.find(symbol);
    if (it == locks_.end() || !it->second.locked) {
        return true;  // Not locked, can trade
    }
    
    // If locked by this engine, it can only unwind
    // Other engines are blocked entirely
    return it->second.locked_engine_id == engine_id;
}

void UnwindCoordinator::check_release(const std::string& symbol, double position) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = locks_.find(symbol);
    if (it == locks_.end() || !it->second.locked) {
        return;  // Not locked
    }
    
    double abs_pos = std::fabs(position);
    
    // Release lock if position has dropped sufficiently
    if (abs_pos < RELEASE_THRESHOLD) {
        std::cout << "[UNWIND] RELEASE " << symbol 
                  << " pos=" << position << "\n";
        it->second.locked = false;
    }
}

} // namespace chimera
