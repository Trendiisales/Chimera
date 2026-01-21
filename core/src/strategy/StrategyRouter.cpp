#include "chimera/strategy/StrategyRouter.hpp"

namespace chimera {

StrategyRouter::StrategyRouter(
    MarketBus& market,
    Microstructure& micro,
    ETHFade& eth,
    BTCCascade& btc
) : market_bus(market),
    microstructure(micro),
    eth_fade(eth),
    btc_cascade(btc) {}

void StrategyRouter::onTick(
    const MarketTick& t
) {
    market_bus.onTick(t);

    microstructure.onTick(
        t.symbol,
        t.bid,
        t.ask,
        t.bid_size,
        t.ask_size,
        t.ts_ns
    );

    double spr = t.ask - t.bid;

    eth_fade.onTick(
        t.symbol,
        t.bid,
        t.ask,
        spr,
        t.ts_ns
    );

    btc_cascade.onTick(
        t.symbol,
        t.bid,
        t.ask,
        spr,
        t.ts_ns
    );
}

}
