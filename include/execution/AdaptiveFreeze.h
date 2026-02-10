#pragma once
#include <cstdint>
#include <cmath>

class AdaptiveFreeze {
public:
    AdaptiveFreeze() 
        : freeze_until_ns_(0)
        , last_velocity_(0.0)
    {}
    
    // Check if currently frozen
    bool is_frozen(uint64_t now_ns) const {
        return now_ns < freeze_until_ns_;
    }
    
    // Check if freeze should be cancelled due to velocity improvement
    bool should_cancel_freeze(double current_velocity, uint64_t now_ns) const {
        if (!is_frozen(now_ns)) return false;
        
        // If velocity improved by 15%+, cancel freeze
        double abs_current = std::abs(current_velocity);
        double abs_last = std::abs(last_velocity_);
        
        if (abs_last > 0.0) {
            double improvement = abs_current / abs_last;
            return improvement >= 1.15; // 15% improvement threshold
        }
        
        return false;
    }
    
    // Set freeze with velocity-based decay
    void set_freeze(uint64_t now_ns, uint64_t base_duration_ms, double velocity) {
        last_velocity_ = velocity;
        
        // Decay freeze duration based on velocity magnitude
        double abs_vel = std::abs(velocity);
        double decay_factor = std::exp(-abs_vel * 10.0); // Exponential decay
        
        uint64_t duration_ns = static_cast<uint64_t>(base_duration_ms * 1'000'000 * decay_factor);
        freeze_until_ns_ = now_ns + duration_ns;
    }
    
    // Clear freeze immediately
    void clear_freeze() {
        freeze_until_ns_ = 0;
    }
    
    // Clear freeze on successful TP
    void on_tp_exit(uint64_t now_ns) {
        clear_freeze();
    }
    
    // Clear freeze on impulse transition
    void on_impulse_transition(uint64_t now_ns) {
        clear_freeze();
    }

private:
    uint64_t freeze_until_ns_;
    double last_velocity_;
};
