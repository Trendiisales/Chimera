// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/control/HysteresisGate.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Prevent oscillation in binary decisions (regime, mode, disable)
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.14: NEW FILE - Core stability system
//
// INVARIANT: "Nothing important changes instantly"
// - Disables, regime flips, mode switches must take time
// - Prevents noise from killing edge
// - Makes system stable under uncertainty
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <algorithm>

namespace Chimera {
namespace Control {

// ─────────────────────────────────────────────────────────────────────────────
// HysteresisGate - Prevents rapid state oscillation
// ─────────────────────────────────────────────────────────────────────────────
struct HysteresisGate {
    bool state = false;
    uint64_t last_change_ms = 0;
    uint32_t min_hold_ms = 2000;  // Default 2 second minimum hold
    
    HysteresisGate() = default;
    
    HysteresisGate(bool initial_state, uint64_t now_ms, uint32_t hold_ms)
        : state(initial_state)
        , last_change_ms(now_ms)
        , min_hold_ms(hold_ms)
    {}
    
    // Returns current state after considering requested change
    // Change only happens if min_hold_ms has elapsed
    bool update(bool requested_state, uint64_t now_ms) noexcept {
        if (requested_state == state) {
            return state;  // No change requested
        }
        
        // Check if enough time has passed to allow state change
        if (now_ms - last_change_ms < min_hold_ms) {
            return state;  // Too soon, keep current state
        }
        
        // Allow state change
        state = requested_state;
        last_change_ms = now_ms;
        return state;
    }
    
    // Force state change (use sparingly - for initialization)
    void force(bool new_state, uint64_t now_ms) noexcept {
        state = new_state;
        last_change_ms = now_ms;
    }
    
    // Time since last change
    [[nodiscard]] uint64_t time_in_state_ms(uint64_t now_ms) const noexcept {
        return now_ms - last_change_ms;
    }
    
    // Check if state is "mature" (held for at least min_hold_ms)
    [[nodiscard]] bool is_mature(uint64_t now_ms) const noexcept {
        return (now_ms - last_change_ms) >= min_hold_ms;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ThresholdHysteresis - For numeric thresholds with bands
// Prevents oscillation around a single threshold value
// ─────────────────────────────────────────────────────────────────────────────
struct ThresholdHysteresis {
    bool above = false;
    double upper_threshold = 0.0;
    double lower_threshold = 0.0;  // Must be < upper for hysteresis band
    
    ThresholdHysteresis() = default;
    
    ThresholdHysteresis(double upper, double lower)
        : above(false)
        , upper_threshold(upper)
        , lower_threshold(lower)
    {}
    
    // Update state based on value
    // Only crosses UP when value > upper_threshold
    // Only crosses DOWN when value < lower_threshold
    bool update(double value) noexcept {
        if (above) {
            // Currently above - need to drop below lower to flip
            if (value < lower_threshold) {
                above = false;
            }
        } else {
            // Currently below - need to rise above upper to flip
            if (value > upper_threshold) {
                above = true;
            }
        }
        return above;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CountHysteresis - Requires N consecutive signals to change state
// ─────────────────────────────────────────────────────────────────────────────
struct CountHysteresis {
    bool state = false;
    int consecutive_count = 0;
    int required_count = 2;  // Default: 2 consecutive signals to flip
    
    CountHysteresis() = default;
    
    explicit CountHysteresis(int required) : required_count(required) {}
    
    bool update(bool signal) noexcept {
        if (signal == state) {
            consecutive_count = 0;  // Reset counter when signal matches state
            return state;
        }
        
        // Signal differs from state
        consecutive_count++;
        
        if (consecutive_count >= required_count) {
            state = signal;
            consecutive_count = 0;
        }
        
        return state;
    }
    
    void reset() noexcept {
        consecutive_count = 0;
    }
};

} // namespace Control
} // namespace Chimera
