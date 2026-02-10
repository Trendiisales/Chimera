#pragma once
#include "execution/RollingStats.h"
#include <cmath>

class VelocityZScore {
public:
    explicit VelocityZScore(size_t window)
        : stats_(window) {}

    void update(double velocity) {
        stats_.push(velocity);
        last_velocity_ = velocity;
    }

    double zscore() const {
        const double sd = stats_.stddev();
        if (sd <= 1e-9) return 0.0;
        return last_velocity_ / sd;
    }

    bool ready() const {
        return stats_.ready();
    }

private:
    RollingStats stats_;
    double last_velocity_ = 0.0;
};
