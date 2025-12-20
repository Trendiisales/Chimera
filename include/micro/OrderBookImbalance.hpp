#pragma once
#include "micro/MicroSignal.hpp"

namespace Chimera {

class OrderBookImbalance {
public:
    OrderBookImbalance();

    MicroSignal on_book(double bid_qty, double ask_qty, uint64_t ts_ns);

private:
    double last_;
};

}
