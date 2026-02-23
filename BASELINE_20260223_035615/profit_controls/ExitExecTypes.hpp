#pragma once
#include <cstdint>

namespace chimera {

enum class ExitExecMode : uint8_t {
    NORMAL = 0,
    AGGRESSIVE_TAKER = 1
};

}
