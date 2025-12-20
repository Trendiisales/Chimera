#include "micro/Microprice.hpp"

using namespace Chimera;

Microprice::Microprice() : last_(0.0) {}

MicroSignal Microprice::on_book(double bid_px, double ask_px,
                                double bid_qty, double ask_qty,
                                uint64_t ts_ns) {
    double denom = bid_qty + ask_qty;
    double micro = denom > 0.0
        ? (bid_px * ask_qty + ask_px * bid_qty) / denom
        : 0.0;
    last_ = micro;
    return {micro, ts_ns};
}
