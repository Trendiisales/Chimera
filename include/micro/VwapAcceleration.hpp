// =============================================================================
// VwapAcceleration.hpp - v4.8.0 - MICRO-VWAP SLOPE ACCELERATION FILTER
// =============================================================================
// PURPOSE: Improves entry quality, removes chop
//
// Predator only trades when VWAP slope is not just positive, but ACCELERATING.
//
// This filters:
//   - False acceptance
//   - Slow drift
//   - Fake breaks
//
// RULE (AUTHORITATIVE):
//   VWAP slope_now > slope_prev
//   AND
//   |slope_now - slope_prev| ≥ accel_threshold
//
// If slope is flat or decelerating → no trade.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace Chimera {

struct VwapAccelState {
    double prevSlope = 0.0;
    double prevPrevSlope = 0.0;
    int sampleCount = 0;
    
    void reset() {
        prevSlope = 0.0;
        prevPrevSlope = 0.0;
        sampleCount = 0;
    }
};

// =============================================================================
// VWAP ACCELERATION CHECK
// =============================================================================
// Returns true if VWAP slope is accelerating (getting steeper in same direction)
inline bool vwapAccelerating(
    double currentSlope,
    VwapAccelState& state,
    double threshold = 0.00015  // Tuned for indices
) {
    // Need at least 2 samples to compute acceleration
    if (state.sampleCount < 2) {
        state.prevPrevSlope = state.prevSlope;
        state.prevSlope = currentSlope;
        state.sampleCount++;
        return false;
    }
    
    // Calculate acceleration (change in slope)
    double accel = currentSlope - state.prevSlope;
    
    // Update state
    state.prevPrevSlope = state.prevSlope;
    state.prevSlope = currentSlope;
    state.sampleCount++;
    
    // Check if accelerating in the direction of the slope
    // For positive slope, acceleration should be positive
    // For negative slope, acceleration should be negative (more negative)
    if (currentSlope > 0) {
        return accel >= threshold;
    } else if (currentSlope < 0) {
        return accel <= -threshold;
    }
    
    return false;
}

// =============================================================================
// VWAP SLOPE DIRECTION CHECK
// =============================================================================
enum class VwapDirection : uint8_t {
    FLAT = 0,
    UP = 1,
    DOWN = 2
};

inline VwapDirection getVwapDirection(double slope, double flatThreshold = 0.00005) {
    if (slope > flatThreshold) return VwapDirection::UP;
    if (slope < -flatThreshold) return VwapDirection::DOWN;
    return VwapDirection::FLAT;
}

inline const char* vwapDirectionToString(VwapDirection d) {
    switch (d) {
        case VwapDirection::UP:   return "UP";
        case VwapDirection::DOWN: return "DOWN";
        case VwapDirection::FLAT: return "FLAT";
        default:                  return "UNKNOWN";
    }
}

// =============================================================================
// COMPREHENSIVE VWAP STATE
// =============================================================================
struct VwapState {
    double currentSlope = 0.0;
    double previousSlope = 0.0;
    double acceleration = 0.0;
    VwapDirection direction = VwapDirection::FLAT;
    bool isAccelerating = false;
    
    void update(double newSlope, double accelThreshold = 0.00015) {
        previousSlope = currentSlope;
        currentSlope = newSlope;
        acceleration = currentSlope - previousSlope;
        direction = getVwapDirection(currentSlope);
        
        // Check acceleration in direction of slope
        if (direction == VwapDirection::UP) {
            isAccelerating = acceleration >= accelThreshold;
        } else if (direction == VwapDirection::DOWN) {
            isAccelerating = acceleration <= -accelThreshold;
        } else {
            isAccelerating = false;
        }
    }
    
    void print() const {
        printf("[VWAP] Slope: %.6f | Dir: %s | Accel: %.6f | %s\n",
               currentSlope,
               vwapDirectionToString(direction),
               acceleration,
               isAccelerating ? "ACCELERATING" : "NOT_ACCELERATING");
    }
};

} // namespace Chimera
