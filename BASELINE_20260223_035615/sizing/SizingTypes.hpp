#pragma once
#include <cstdint>

namespace chimera {

enum class SizingAction : uint8_t {
    HOLD_BASE = 0,
    SCALE_UP = 1,
    SCALE_DOWN = 2,
    ZERO = 3
};

}
