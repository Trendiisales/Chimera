#pragma once
#include <cstdint>
#include "ExecPolicyTypes.hpp"

namespace chimera {

struct ExecPolicyState {
    ExecMode mode = ExecMode::POST_ONLY;

    bool trading_enabled = true;
    bool hard_kill = false;

    double size_multiplier = 1.0;

    uint64_t last_update_ns = 0;
};

}
