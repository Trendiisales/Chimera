#pragma once
#include <cstdint>

namespace chimera {

enum class TelemetryType : uint8_t {
    LATENCY = 0,
    EXEC_POLICY = 1,
    CAPITAL = 2,
    RISK_EVENT = 3,
    SYSTEM = 4
};

}
