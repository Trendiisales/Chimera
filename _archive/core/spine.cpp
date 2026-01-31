#include "spine.hpp"
#include <iostream>

namespace chimera {

Spine::Spine() : trade_count_(0) {
    telemetry_.online = true;
    telemetry_.trading = false;
    telemetry_.btc_price = 0.0;
    telemetry_.eth_price = 0.0;
    telemetry_.trades = 0;
}

void Spine::registerEngine(IEngine* engine) {
    engines_.push_back(engine);
}

void Spine::onTick(const MarketTick& tick) {
    if (tick.symbol == "BTCUSDT") telemetry_.btc_price = (tick.bid + tick.ask) * 0.5;
    if (tick.symbol == "ETHUSDT") telemetry_.eth_price = (tick.bid + tick.ask) * 0.5;

    std::vector<OrderIntent> intents;
    for (auto* e : engines_) {
        e->onTick(tick, intents);
    }

    for (const auto& intent : intents) {
        trade_count_++;
        telemetry_.trades = trade_count_;
        telemetry_.trading = true;
        std::cout << "[EXEC] SENT "
                  << intent.symbol << " "
                  << (intent.is_buy ? "BUY" : "SELL")
                  << " " << intent.size
                  << " @ " << intent.price
                  << " (" << intent.engine_id << ")"
                  << std::endl;
    }
}

const ChimeraTelemetry& Spine::telemetry() const {
    return telemetry_;
}

}
