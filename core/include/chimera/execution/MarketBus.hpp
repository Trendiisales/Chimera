#pragma once

#include <string>
#include <unordered_map>
#include "chimera/execution/ExchangeIO.hpp"

namespace chimera {

class MarketBus {
public:
    void onTick(const MarketTick& t);

    double spread(const std::string& symbol) const;
    double volatility(const std::string& symbol) const;
    double last(const std::string& symbol) const;

private:
    std::unordered_map<std::string, MarketTick> last_tick;
};

}
