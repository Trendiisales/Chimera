#pragma once
#include <cstdint>

namespace chimera {

enum class ExecMode : uint8_t {
    DISABLED  = 0,
    POST_ONLY = 1,
    TAKE_ONLY = 2
};

}
