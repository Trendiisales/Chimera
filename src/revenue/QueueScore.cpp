#include "revenue/QueueScore.hpp"
#include <cmath>
#include <algorithm>

namespace chimera {

QueueScore::QueueScore() {}

void QueueScore::on_submit(const std::string& client_id,
                           double size,
                           uint64_t timestamp_ns) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    QueueState state;
    state.submit_time_ns = timestamp_ns;
    state.traded_through_volume = 0.0;
    state.our_size = size;
    
    orders_[client_id] = state;
}

void QueueScore::on_trade_through(const std::string& client_id, double volume) {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = orders_.find(client_id);
    if (it != orders_.end()) {
        it->second.traded_through_volume += volume;
    }
}

double QueueScore::calculate_score(const std::string& client_id,
                                   uint64_t current_time_ns) const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = orders_.find(client_id);
    if (it == orders_.end()) {
        return 0.0;  // Unknown order = worst score
    }
    
    const QueueState& state = it->second;
    
    // Age factor: decays from 1.0 to 0.0 as order ages
    uint64_t age_ns = current_time_ns - state.submit_time_ns;
    double age_factor = std::exp(-static_cast<double>(age_ns) / AGE_DECAY_NS);
    
    // Volume factor: decays as volume trades through our level
    // Normalized by our order size
    double volume_ratio = state.traded_through_volume / 
                         std::max(state.our_size, 0.001);
    double volume_factor = std::exp(-volume_ratio / VOLUME_DECAY);
    
    // Combined score: both factors must be good
    // If either age or volume is bad, score is low
    double score = age_factor * volume_factor;
    
    return std::max(0.0, std::min(1.0, score));
}

bool QueueScore::should_replace(const std::string& client_id,
                                uint64_t current_time_ns,
                                double threshold) const {
    double score = calculate_score(client_id, current_time_ns);
    return score < threshold;
}

void QueueScore::on_done(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    orders_.erase(client_id);
}

} // namespace chimera
