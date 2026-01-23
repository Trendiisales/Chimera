#pragma once
#include <functional>
#include <string>

namespace chimera::replay {

struct MarketTick {
    std::string symbol;
    double bid;
    double ask;
    double bid_size;
    double ask_size;
    uint64_t ts_ns;
};

class ReplayBus {
public:
    using TickHandler = std::function<void(const MarketTick&)>;

    void attach(TickHandler h) {
        handler = h;
    }

    void inject(const MarketTick& t) {
        if (handler) handler(t);
    }

private:
    TickHandler handler;
};

}
