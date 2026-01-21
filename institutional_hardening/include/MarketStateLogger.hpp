#pragma once
#include <cstdint>

namespace chimera {
namespace hardening {

struct MarketState {
    double best_bid;
    double best_ask;
    double spread_bps;
    double imbalance;
    double depth_top5;
    double venue_bid[3];
    double venue_ask[3];
    int64_t timestamp_ns;
};

class MarketStateLogger {
public:
    static MarketState capture(
        double bid,
        double ask,
        double bid_qty,
        double ask_qty,
        int64_t timestamp_ns = 0
    );
};

}} // namespace chimera::hardening
