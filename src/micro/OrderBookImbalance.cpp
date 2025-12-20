#include "micro/OrderBookImbalance.hpp"

using namespace Chimera;

OrderBookImbalance::OrderBookImbalance() : last_(0.0) {}

MicroSignal OrderBookImbalance::on_book(double bid_qty, double ask_qty, uint64_t ts_ns) {
    double denom = bid_qty + ask_qty;
    double v = (denom > 0.0) ? (bid_qty - ask_qty) / denom : 0.0;
    last_ = v;
    return {v, ts_ns};
}
