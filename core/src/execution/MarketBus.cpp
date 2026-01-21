#include "chimera/execution/MarketBus.hpp"
#include <cmath>

namespace chimera {

void MarketBus::onTick(const MarketTick& t) {
    last_tick[t.symbol] = t;
}

double MarketBus::spread(
    const std::string& symbol
) const {
    auto it = last_tick.find(symbol);
    if (it == last_tick.end()) return 0.0;
    return it->second.ask - it->second.bid;
}

double MarketBus::volatility(
    const std::string& symbol
) const {
    auto it = last_tick.find(symbol);
    if (it == last_tick.end()) return 0.0;
    return std::abs(it->second.ask - it->second.last);
}

double MarketBus::last(
    const std::string& symbol
) const {
    auto it = last_tick.find(symbol);
    if (it == last_tick.end()) return 0.0;
    return it->second.last;
}

}
