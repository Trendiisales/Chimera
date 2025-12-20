#pragma once
#include "micro/MicroEnginePack.hpp"
#include "signal/SignalTypes.hpp"
#include "signal/SignalNormalizer.hpp"

namespace Chimera {

class SignalAggregator {
public:
    SignalAggregator();

    AggregatedSignal on_book(
        double bid_px,
        double ask_px,
        double bid_qty,
        double ask_qty,
        uint64_t ts_ns
    );

    AggregatedSignal on_trade(
        double qty,
        bool is_buy,
        double price,
        uint64_t ts_ns
    );

private:
    MicroEnginePack micro_;

    SignalNormalizer norm_obi_;
    SignalNormalizer norm_micro_;
    SignalNormalizer norm_flow_;
    SignalNormalizer norm_vol_;

    AggregatedSignal last_;
};

}
