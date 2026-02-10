#pragma once
#include <cmath>

struct VelocityTracker {
    double last_mid = 0.0;
    double velocity = 0.0;

    inline void update(double mid) {
        if (last_mid != 0.0) {
            velocity = mid - last_mid;
        }
        last_mid = mid;
    }

    inline double abs() const {
        return std::fabs(velocity);
    }
};
