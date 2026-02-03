#pragma once
#include <chrono>

namespace chimera {

// AlphaDecay: Track how edge decays over time
// Use to decide if order is still worth keeping in queue
class AlphaDecay {
public:
    AlphaDecay(double half_life_ms = 1000.0) 
        : half_life_ms_(half_life_ms) {}
    
    // Calculate current edge given initial edge and time elapsed
    double current_edge(double initial_edge_bps, long elapsed_ms) const {
        // Exponential decay: edge(t) = edge_0 * exp(-t / tau)
        double tau = half_life_ms_ / 0.693147;  // tau = half_life / ln(2)
        return initial_edge_bps * std::exp(-elapsed_ms / tau);
    }
    
    // Check if edge has decayed below threshold
    bool decayed_below(double initial_edge_bps, long elapsed_ms, double threshold_bps) const {
        return current_edge(initial_edge_bps, elapsed_ms) < threshold_bps;
    }
    
    // Get time until edge decays to threshold
    long time_to_decay(double initial_edge_bps, double threshold_bps) const {
        if (initial_edge_bps <= threshold_bps) return 0;
        double tau = half_life_ms_ / 0.693147;
        return static_cast<long>(tau * std::log(initial_edge_bps / threshold_bps));
    }

private:
    double half_life_ms_;
};

} // namespace chimera
