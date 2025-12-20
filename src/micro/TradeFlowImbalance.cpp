#include "micro/TradeFlowImbalance.hpp"

using namespace Chimera;

TradeFlowImbalance::TradeFlowImbalance()
    : buy_vol_(0.0), sell_vol_(0.0) {}

MicroSignal TradeFlowImbalance::on_trade(double qty, bool is_buy, uint64_t ts_ns) {
    if (is_buy)
        buy_vol_ += qty;
    else
        sell_vol_ += qty;

    double denom = buy_vol_ + sell_vol_;
    double v = denom > 0.0 ? (buy_vol_ - sell_vol_) / denom : 0.0;
    return {v, ts_ns};
}
