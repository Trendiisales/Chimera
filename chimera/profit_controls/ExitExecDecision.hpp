#pragma once
#include <cstdint>
#include "ExitExecTypes.hpp"

namespace chimera {

struct ExitExecDecision {
    uint64_t causal_id = 0;
    ExitExecMode mode = ExitExecMode::NORMAL;
    uint64_t ts_ns = 0;
};

}
