#include "BTCascade.hpp"

namespace chimera {

BTCascade::BTCascade() : engine_id_("BTC_CASCADE"), last_mid_(0.0) {}

const std::string& BTCascade::id() const {
    return engine_id_;
}

void BTCascade::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    if (tick.symbol != "BTCUSDT") return;

    double mid = (tick.bid + tick.ask) * 0.5;
    if (last_mid_ == 0.0) {
        last_mid_ = mid;
        return;
    }

    double delta = mid - last_mid_;
    last_mid_ = mid;

    if (delta > 2.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = tick.ask;
        o.size = 0.01;
        out.push_back(o);
    }

    if (delta < -2.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = tick.bid;
        o.size = 0.01;
        out.push_back(o);
    }
}

}
