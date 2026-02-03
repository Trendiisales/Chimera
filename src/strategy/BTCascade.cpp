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

    // ---------------------------------------------------------------------------
    // Relative momentum in bps. Absolute $2 threshold on BTC ($78k) = 0.003% =
    // tick noise — fired on every price change. 15bps = $11.7 on BTC. Fires
    // only on real momentum moves.
    // ---------------------------------------------------------------------------
    double delta_bps = (mid - last_mid_) / last_mid_ * 10000.0;
    last_mid_ = mid;

    // ---------------------------------------------------------------------------
    // Position cap: max 0.05 BTC per direction. tick.position injected by
    // StrategyRunner. Without this, momentum chasing accumulates unbounded.
    // ---------------------------------------------------------------------------
    static constexpr double MAX_POS = 0.05;
    double abs_pos = (tick.position > 0) ? tick.position : -tick.position;
    if (abs_pos >= MAX_POS) return;

    if (delta_bps > 15.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = tick.ask;
        o.size = 0.01;
        out.push_back(o);
    }

    if (delta_bps < -15.0) {
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
