#pragma once
#include "micro/MicroSignal.hpp"

namespace Chimera {

class TradeFlowImbalance {
public:
    TradeFlowImbalance();

    MicroSignal on_trade(double qty, bool is_buy, uint64_t ts_ns);

private:
    double buy_vol_;
    double sell_vol_;
};

}
