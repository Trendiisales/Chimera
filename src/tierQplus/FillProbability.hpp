#pragma once
#include <unordered_map>
#include <string>
#include <vector>

namespace chimera {

// FillProbability: ML-based fill probability estimation
// Uses simple online learning to predict fill likelihood
struct FillFeatures {
    double queue_position{0.0};   // Normalized position in queue
    double edge_bps{0.0};          // Current edge
    double volume_ratio{0.0};      // Recent/avg volume
    double spread_bps{0.0};        // Bid-ask spread
    double volatility{0.0};        // Recent price volatility
};

class FillProbability {
public:
    FillProbability() {
        // Initialize simple weights
        weights_ = {0.4, 0.3, 0.15, 0.1, 0.05};  // queue, edge, volume, spread, vol
    }
    
    double predict(const FillFeatures& features) const {
        double score = 
            weights_[0] * (1.0 - features.queue_position) +
            weights_[1] * std::min(features.edge_bps / 10.0, 1.0) +
            weights_[2] * std::min(features.volume_ratio / 2.0, 1.0) +
            weights_[3] * (1.0 - std::min(features.spread_bps / 5.0, 1.0)) +
            weights_[4] * (1.0 - std::min(features.volatility, 1.0));
        
        // Sigmoid to convert to probability
        return 1.0 / (1.0 + std::exp(-5.0 * (score - 0.5)));
    }
    
    void update(const FillFeatures& features, bool filled) {
        // Simple online gradient descent
        double pred = predict(features);
        double error = (filled ? 1.0 : 0.0) - pred;
        double lr = 0.01;  // Learning rate
        
        std::vector<double> feature_vec = {
            1.0 - features.queue_position,
            std::min(features.edge_bps / 10.0, 1.0),
            std::min(features.volume_ratio / 2.0, 1.0),
            1.0 - std::min(features.spread_bps / 5.0, 1.0),
            1.0 - std::min(features.volatility, 1.0)
        };
        
        for (size_t i = 0; i < weights_.size(); ++i) {
            weights_[i] += lr * error * feature_vec[i];
        }
    }

private:
    std::vector<double> weights_;
};

} // namespace chimera
