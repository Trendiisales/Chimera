#pragma once
#include <string>
#include <map>
#include <mutex>
#include <cmath>

namespace chimera {

// ---------------------------------------------------------------------------
// UnwindCoordinator prevents engines from fighting at position caps.
//
// Problem: Multiple engines polling independently can all see position=0.04,
// all decide to trade, all submit orders simultaneously → position=0.07+
// (violates 0.05 cap).
//
// Solution: Locking mechanism that:
//   1. When position reaches cap → locks that symbol+engine
//   2. Locked engine can ONLY unwind (close position)
//   3. Lock releases when position drops below threshold
//
// Thread-safe: All methods use mutex, called from multiple StrategyRunner
// threads polling independently.
// ---------------------------------------------------------------------------

class UnwindCoordinator {
public:
    // Try to lock this symbol+engine if at position cap
    // Called at start of engine's onTick()
    void try_lock(const std::string& symbol, const std::string& engine_id, 
                  double position);
    
    // Check if this engine is allowed to trade
    // Returns false if locked (must unwind)
    bool can_trade(const std::string& symbol, const std::string& engine_id) const;
    
    // Check if position has dropped enough to release lock
    // Called after position check in onTick()
    void check_release(const std::string& symbol, double position);
    
private:
    static constexpr double MAX_POSITION = 0.05;   // Cap per symbol
    static constexpr double RELEASE_THRESHOLD = 0.03;  // Release lock when position drops below this
    
    struct LockState {
        bool locked;
        std::string locked_engine_id;  // Which engine hit the cap
        double lock_position;          // Position when locked
    };
    
    mutable std::mutex mtx_;
    std::map<std::string, LockState> locks_;  // symbol → lock state
    
    std::string make_key(const std::string& symbol, const std::string& engine_id) const {
        return symbol + ":" + engine_id;
    }
};

} // namespace chimera
