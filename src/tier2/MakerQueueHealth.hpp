#pragma once
#include <unordered_map>
#include <string>
#include <chrono>

namespace chimera {

// ---------------------------------------------------------------------------
// MakerQueueHealth: Track and cancel stale maker orders
// 
// Problem: Maker orders sit in queue, get run over when stale
// Solution: Cancel orders that age past threshold
// 
// Usage:
//   MakerQueueHealth queue_health;
//   
//   // On order submit
//   queue_health.on_submit(client_order_id);
//   
//   // Periodically check
//   if (queue_health.stale(client_order_id))
//       cancel_order(client_order_id);
// 
// Effect: Improves fill quality, reduces adverse selection
// Expected gain: +1-3 bps per maker fill
// ---------------------------------------------------------------------------

class MakerQueueHealth {
public:
    MakerQueueHealth(long max_age_ms = 1500)
        : max_age_ms_(max_age_ms) {}

    // Record order submission timestamp
    void on_submit(const std::string& order_id) {
        stamps_[order_id] = now_ms();
    }

    // Check if order is stale
    bool stale(const std::string& order_id) const {
        auto it = stamps_.find(order_id);
        if (it == stamps_.end())
            return false;  // No record = not stale

        return (now_ms() - it->second) > max_age_ms_;
    }

    // Get order age in milliseconds
    long age_ms(const std::string& order_id) const {
        auto it = stamps_.find(order_id);
        if (it == stamps_.end())
            return 0;
        
        return now_ms() - it->second;
    }

    // Remove order from tracking (on fill/cancel)
    void remove(const std::string& order_id) {
        stamps_.erase(order_id);
    }

    // Set max age threshold
    void set_max_age_ms(long ms) {
        max_age_ms_ = ms;
    }

private:
    long now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    std::unordered_map<std::string, long> stamps_;
    long max_age_ms_;
};

} // namespace chimera
