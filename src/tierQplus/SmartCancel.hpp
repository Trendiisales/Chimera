#pragma once
#include <unordered_map>
#include <string>
#include <chrono>

namespace chimera {

// SmartCancel: Adaptively cancel orders based on multiple signals
struct CancelSignals {
    bool queue_degraded{false};    // Queue position worsened
    bool edge_decayed{false};       // Edge below threshold
    bool market_moved{false};       // Price moved away
    bool volume_surge{false};       // Volume spike (might get run over)
    
    int score() const {
        return queue_degraded + edge_decayed + market_moved + volume_surge;
    }
    
    bool should_cancel(int threshold = 2) const {
        return score() >= threshold;
    }
};

class SmartCancel {
public:
    SmartCancel(int cancel_threshold = 2) 
        : cancel_threshold_(cancel_threshold) {}
    
    void update_signals(const std::string& order_id, const CancelSignals& signals) {
        signals_[order_id] = signals;
        timestamps_[order_id] = now_ms();
    }
    
    bool should_cancel(const std::string& order_id) const {
        auto it = signals_.find(order_id);
        if (it == signals_.end()) return false;
        return it->second.should_cancel(cancel_threshold_);
    }
    
    CancelSignals get_signals(const std::string& order_id) const {
        auto it = signals_.find(order_id);
        return (it != signals_.end()) ? it->second : CancelSignals{};
    }
    
    void clear(const std::string& order_id) {
        signals_.erase(order_id);
        timestamps_.erase(order_id);
    }

private:
    long now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    int cancel_threshold_;
    std::unordered_map<std::string, CancelSignals> signals_;
    std::unordered_map<std::string, long> timestamps_;
};

} // namespace chimera
