#include "signal/SignalAggregator.hpp"

using namespace Chimera;

SignalAggregator::SignalAggregator()
    : norm_obi_(1.0),
      norm_micro_(1.0),
      norm_flow_(1.0),
      norm_vol_(0.5) {
    last_ = {};
}

AggregatedSignal SignalAggregator::on_book(
    double bid_px,
    double ask_px,
    double bid_qty,
    double ask_qty,
    uint64_t ts_ns
) {
    auto obi = micro_.obi.on_book(bid_qty, ask_qty, ts_ns);
    auto mp  = micro_.microprice.on_book(
        bid_px, ask_px, bid_qty, ask_qty, ts_ns
    );

    last_.obi = norm_obi_.norm(obi.value);
    last_.microprice = norm_micro_.norm(mp.value - ((bid_px + ask_px) * 0.5));
    last_.ts_ns = ts_ns;

    last_.composite =
        0.4 * last_.obi +
        0.4 * last_.microprice +
        0.2 * last_.flow;

    return last_;
}

AggregatedSignal SignalAggregator::on_trade(
    double qty,
    bool is_buy,
    double price,
    uint64_t ts_ns
) {
    auto flow = micro_.tfi.on_trade(qty, is_buy, ts_ns);
    auto vol  = micro_.vol.on_price(price, ts_ns);

    last_.flow = norm_flow_.norm(flow.value);
    last_.volatility = norm_vol_.norm(vol.value);
    last_.ts_ns = ts_ns;

    last_.composite =
        0.35 * last_.obi +
        0.35 * last_.microprice +
        0.2  * last_.flow -
        0.1  * last_.volatility;

    return last_;
}
