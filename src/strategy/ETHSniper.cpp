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

    // ---------------------------------------------------------------------------
    // Relative impulse in bps. Absolute $1.50 on ETH ($2400) = 6.25bps — very
    // noisy. 20bps = $0.48 on ETH. Still sensitive but only fires on real
    // impulse moves, not every tick.
    // ---------------------------------------------------------------------------
    double impulse_bps = (mid - last_mid_) / last_mid_ * 10000.0;
    last_mid_ = mid;

    // ---------------------------------------------------------------------------
    // Position cap: max 0.5 ETH per direction. tick.position injected by
    // StrategyRunner. Sniper accumulates fast on impulse — cap prevents blowout.
    // ---------------------------------------------------------------------------
    static constexpr double MAX_POS = 0.5;
    double abs_pos = (tick.position > 0) ? tick.position : -tick.position;
    if (abs_pos >= MAX_POS) return;

    if (impulse_bps > 20.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = tick.ask;
        o.size = 0.02;
        out.push_back(o);
    }

    if (impulse_bps < -20.0) {
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
