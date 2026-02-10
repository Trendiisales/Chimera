#pragma once
#include "execution/LatencyStats.h"
#include <cmath>

struct XAUImpulseDecisionZ {
    bool allowed;
    bool soft;
};

class XAUImpulseGateZ {
public:
    static XAUImpulseDecisionZ evaluate(
        double z,
        double spread,
        int legs,
        const LatencyStats& lat
    ) {
        const double az = std::fabs(z);

        // HARD Z impulse
        if (az >= 2.4) {
            return { true, false };
        }

        // SOFT Z impulse — very tightly gated
        if (
            az >= 1.2 &&
            lat.is_fast() &&
            spread <= 0.30 &&
            legs == 0
        ) {
            return { true, true };
        }

        return { false, false };
    }
};
