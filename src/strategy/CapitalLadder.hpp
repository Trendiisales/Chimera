#pragma once
#include <cstdint>
#include <cmath>

namespace chimera {

// Helper for scaling position sizes in ladders
class CapitalLadder {
public:
    CapitalLadder()
        : base_notional_(0.01)
        , scale_step_(1.5)
        , max_layers_(4)
    {}

    void set_base_notional(double notional) { base_notional_ = notional; }
    void set_scale_step(double step) { scale_step_ = step; }
    void set_max_layers(int layers) { max_layers_ = layers; }

    // Calculate size for next layer
    double calculate_size(int current_layer) const {
        if (current_layer >= max_layers_) return 0.0;
        
        double size = base_notional_;
        for (int i = 0; i < current_layer; ++i) {
            size *= scale_step_;
        }
        return size;
    }

    // Check if can add more layers
    bool can_add_layer(int current_layer) const {
        return current_layer < max_layers_;
    }

    int get_max_layers() const { return max_layers_; }

private:
    double base_notional_;
    double scale_step_;
    int max_layers_;
};

}
