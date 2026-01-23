#pragma once

#include "chimera/infra/Clock.hpp"
#include "chimera/core/Events.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace chimera::causal {

using event_id_t = uint64_t;
using symbol_hash_t = uint32_t;

// Use enforced monotonic clock
inline uint64_t steady_ns() {
    return infra::to_ns(infra::now());
}

enum class EventType : uint8_t {
    TICK = 1,
    DECISION = 2,
    RISK = 3,
    ORDER_INTENT = 4,
    VENUE_ACK = 5,
    FILL = 6,
    PNL_ATTRIBUTION = 7
};

struct EventHeader {
    event_id_t id;
    event_id_t parent_id;
    EventType type;
    uint64_t ts_ns;
    symbol_hash_t symbol;
};

struct TickEvent {
    EventHeader h;
    double bid;
    double ask;
    double bid_sz;
    double ask_sz;
};

struct DecisionEvent {
    EventHeader h;
    uint32_t engine_id;
    double edge_score;
    std::array<double, 8> signal_vector;
};

struct RiskEvent {
    EventHeader h;
    bool allowed;
    double max_pos;
    double cur_pos;
};

struct OrderIntentEvent {
    EventHeader h;
    bool is_buy;
    double price;
    double qty;
};

struct VenueAckEvent {
    EventHeader h;
    bool accepted;
    uint32_t venue_code;
};

struct FillEvent {
    EventHeader h;
    double fill_price;
    double fill_qty;
};

struct PnLAttributionEvent {
    EventHeader h;
    double pnl;
    double fee;
    uint32_t engine_id;
};

}
