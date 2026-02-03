#pragma once
#include <unordered_map>
#include <string>

namespace chimera {

// QueueScore: Track our position in order queue
// Better queue position = higher fill probability
struct QueuePosition {
    int our_level{0};        // Our position in queue (0 = front)
    int total_ahead{0};      // Total volume ahead of us
    double fill_prob{0.0};   // Estimated fill probability
};

class QueueScore {
public:
    void on_order_placed(const std::string& order_id, double price, double qty) {
        positions_[order_id] = {0, 0, 1.0};  // Start at front
    }
    
    void on_book_update(const std::string& order_id, int level, int ahead) {
        auto it = positions_.find(order_id);
        if (it != positions_.end()) {
            it->second.our_level = level;
            it->second.total_ahead = ahead;
            it->second.fill_prob = calc_fill_prob(level, ahead);
        }
    }
    
    double fill_probability(const std::string& order_id) const {
        auto it = positions_.find(order_id);
        return (it != positions_.end()) ? it->second.fill_prob : 0.0;
    }
    
    bool should_cancel(const std::string& order_id, double threshold = 0.1) const {
        return fill_probability(order_id) < threshold;
    }

private:
    double calc_fill_prob(int level, int ahead) const {
        // Simple model: prob = exp(-level * 0.5)
        return std::exp(-level * 0.5);
    }
    
    std::unordered_map<std::string, QueuePosition> positions_;
};

} // namespace chimera
