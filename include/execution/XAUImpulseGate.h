#pragma once
#include "execution/LatencyStats.h"
#include <cmath>

struct XAUImpulseDecision {
    bool allowed;
    bool soft;
};

class XAUImpulseGate {
public:
    static XAUImpulseDecision evaluate(
        double velocity,
        double spread,
        int current_legs,
        const LatencyStats& lat
    ) {
        const double abs_vel = std::fabs(velocity);

        // HARD impulse — always allowed
        if (abs_vel >= 0.18) {
            return { true, false };
        }

        // SOFT impulse — strictly gated
        if (
            abs_vel >= 0.08 &&
            lat.is_fast() &&
            spread <= 0.30 &&
            current_legs == 0
        ) {
            return { true, true };
        }

        return { false, false };
    }
};
