#pragma once
#include <cstdint>

namespace chimera {

enum class EventType : uint16_t {
    MARKET_TICK = 1,
    DECISION    = 2,
    ROUTE       = 3,
    ACK         = 4,
    FILL        = 5,
    RISK_BLOCK  = 6,
    THROTTLE    = 7,
    DRIFT       = 8,
    HEARTBEAT   = 9,
    CANCEL      = 10,
    REJECT      = 11
};

struct EventHeader {
    uint64_t  ts_ns;
    uint64_t  causal_id;
    EventType type;
    uint32_t  size;
    uint32_t  crc;
};

}
