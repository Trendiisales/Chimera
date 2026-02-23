#pragma once
#include <string>
#include <cstdint>
#include "TelemetryTypes.hpp"

namespace chimera {

struct TelemetryEvent {
    TelemetryType type;
    uint64_t ts_ns;
    std::string payload_json;
};

}
