#pragma once

#include "chimera/execution/MarketBus.hpp"
#include "chimera/strategy/Microstructure.hpp"
#include "chimera/strategy/ETHFade.hpp"
#include "chimera/strategy/BTCCascade.hpp"

namespace chimera {

class StrategyRouter {
public:
    StrategyRouter(
        MarketBus& market,
        Microstructure& micro,
        ETHFade& eth,
        BTCCascade& btc
    );

    void onTick(const MarketTick& t);

private:
    MarketBus& market_bus;
    Microstructure& microstructure;
    ETHFade& eth_fade;
    BTCCascade& btc_cascade;
};

}
