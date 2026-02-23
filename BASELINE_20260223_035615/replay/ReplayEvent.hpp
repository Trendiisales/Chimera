#pragma once
#include <string>
#include <cstdint>
#include "ReplayEventType.hpp"

namespace chimera {

struct ReplayEvent {
    ReplayEventType type;
    uint64_t ts_ns;
    uint64_t causal_id;
    std::string payload_json;
};

}
