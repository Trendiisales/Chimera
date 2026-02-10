#pragma once
#include <cmath>

struct ImpulseEval {
    double velocity;
    double impulse;
    double abs_impulse;
};

inline ImpulseEval eval_impulse(double vel) {
    return {
        vel,
        vel,
        std::abs(vel)
    };
}
