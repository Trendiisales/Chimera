#pragma once
#include <cstdint>

namespace Chimera {

enum class AlertLevel : uint8_t {
    INFO = 1,
    WARN = 2,
    CRITICAL = 3
};

enum class AlertCode : uint16_t {
    FIX_HALTED        = 1001,
    BINANCE_BLIND     = 1002,
    EXEC_THROTTLED    = 1003,
    DIVERGENCE        = 2001
};

struct alignas(64) AlertEvent {
    uint64_t ts_ns;
    uint16_t code;
    uint8_t  level;
};

}
