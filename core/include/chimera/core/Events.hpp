#pragma once

#include <cstdint>
#include <string>
#include "chimera/infra/Clock.hpp"

namespace chimera::core {

using EventID = uint64_t;

struct TickEvent {
    EventID id;
    std::string symbol;
    double bid;
    double ask;
    infra::MonoTime ts;
};

struct DecisionEvent {
    EventID id;
    std::string engine;
    std::string symbol;
    bool buy;
    double qty;
    infra::MonoTime ts;
};

struct OrderEvent {
    EventID id;
    std::string venue;
    std::string symbol;
    double qty;
    double price;
    infra::MonoTime ts;
};

struct FillEvent {
    EventID id;
    std::string venue;
    std::string symbol;
    double qty;
    double price;
    double fee;
    infra::MonoTime ts;
};

} // namespace chimera::core
