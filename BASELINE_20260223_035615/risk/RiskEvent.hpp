#pragma once
#include <cstdint>

namespace chimera {

enum class RiskEventType : uint8_t {
    LOSS_CLUSTER = 0,
    LATENCY_DRIVEN = 1,
    SLIPPAGE_SPIRAL = 2
};

struct RiskEvent {
    RiskEventType type;
    uint64_t ts_ns;
    double severity;
};

}
