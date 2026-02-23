#pragma once
#include <cstdint>
#include "SizingTypes.hpp"

namespace chimera {

struct SizingDecision {
    uint64_t causal_id = 0;
    SizingAction action = SizingAction::HOLD_BASE;

    double final_size = 0.0;
    double confidence = 0.0;

    uint64_t decision_ts_ns = 0;
};

}
