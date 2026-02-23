#pragma once
#include <cstdint>

namespace chimera {

enum class EscalationAction : uint8_t {
    STAY_POST_ONLY = 0,
    ESCALATE_TO_TAKER = 1,
    ABORT_TRADE = 2
};

}
