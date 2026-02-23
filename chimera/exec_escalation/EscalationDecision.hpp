#pragma once
#include <cstdint>
#include "EscalationTypes.hpp"

namespace chimera {

struct EscalationDecision {
    uint64_t causal_id = 0;
    EscalationAction action = EscalationAction::STAY_POST_ONLY;
    double confidence = 0.0;
    uint64_t decision_ts_ns = 0;
};

}
