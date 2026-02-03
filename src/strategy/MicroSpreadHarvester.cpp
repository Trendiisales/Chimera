#include "MicroSpreadHarvester.hpp"
#include <cmath>

namespace chimera {

MicroSpreadHarvester::MicroSpreadHarvester() 
    : engine_id_("MICRO_SPREAD"), last_spread_(0.0) {}

const std::string& MicroSpreadHarvester::id() const {
    return engine_id_;
}

void MicroSpreadHarvester::onRestore() {
    last_spread_ = 0.0;
}

void MicroSpreadHarvester::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    double spread = tick.ask - tick.bid;
    last_spread_ = spread;

    constexpr double MAX_SPREAD = 0.0005;

    if (spread <= 0.0 || spread > MAX_SPREAD) return;
    
    // Only trade if position is small
    if (std::abs(tick.position) > 0.02) return;

    // Post on both sides to capture spread
    OrderIntent buy;
    buy.engine_id = engine_id_;
    buy.symbol = tick.symbol;
    buy.is_buy = true;
    buy.price = tick.bid;
    buy.size = 0.005;

    OrderIntent sell;
    sell.engine_id = engine_id_;
    sell.symbol = tick.symbol;
    sell.is_buy = false;
    sell.price = tick.ask;
    sell.size = 0.005;

    out.push_back(buy);
    out.push_back(sell);
}

}
