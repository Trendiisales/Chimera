#pragma once
#include <cstdint>

namespace chimera {

enum class ReplayEventType : uint8_t {
    MARKET = 0,
    SIGNAL = 1,
    DECISION = 2,
    ORDER = 3,
    ACK = 4,
    FILL = 5,
    CANCEL = 6,
    POLICY = 7,
    RISK = 8
};

}
