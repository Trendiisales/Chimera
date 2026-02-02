#include "ETHSniper.hpp"

namespace chimera {

ETHSniper::ETHSniper() : engine_id_("ETH_SNIPER"), last_mid_(0.0) {}

const std::string& ETHSniper::id() const {
    return engine_id_;
}

void ETHSniper::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    if (tick.symbol != "ETHUSDT") return;

    double mid = (tick.bid + tick.ask) * 0.5;
    if (last_mid_ == 0.0) {
        last_mid_ = mid;
        return;
    }

    double impulse = mid - last_mid_;
    last_mid_ = mid;

    if (impulse > 1.5) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = tick.ask;
        o.size = 0.02;
        out.push_back(o);
    }

    if (impulse < -1.5) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = tick.bid;
        o.size = 0.02;
        out.push_back(o);
    }
}

}
