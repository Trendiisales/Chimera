#pragma once
#include <cstdint>
#include <string>
#include <array>

namespace chimera_lab {

enum class EventType : uint8_t {
    TICK = 1,
    SIGNAL = 2,
    DECISION = 3,
    ORDER = 4,
    FILL = 5,
    HEARTBEAT = 6
};

struct SignalVector {
    double ofi;
    double impulse;
    double spread;
    double depth;
    double toxic;
    double vpin;
    double funding;
    double regime;
};

struct EventHeader {
    uint64_t event_id;
    uint64_t ts_exchange;
    uint64_t ts_local;
    uint32_t symbol_hash;
    uint8_t  venue;
    uint8_t  engine_id;
    EventType type;
    uint32_t payload_size;
    uint32_t crc32;
};

struct DecisionPayload {
    bool trade;
    double qty;
    double price;
    SignalVector signals;
};

struct FillPayload {
    double fill_price;
    double fill_qty;
    double fee_bps;
    double latency_ms;
};

} // namespace chimera_lab
