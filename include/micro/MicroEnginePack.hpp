#pragma once
#include "micro/OrderBookImbalance.hpp"
#include "micro/Microprice.hpp"
#include "micro/TradeFlowImbalance.hpp"
#include "micro/VolatilityBurst.hpp"

namespace Chimera {

struct MicroEnginePack {
    OrderBookImbalance obi;
    Microprice microprice;
    TradeFlowImbalance tfi;
    VolatilityBurst vol;
};

}
