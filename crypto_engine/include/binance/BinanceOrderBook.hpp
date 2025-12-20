#pragma once

#include "BinanceTypes.hpp"

#include <vector>
#include <cstddef>

namespace binance {

/*
 Single-writer, multi-reader snapshot order book.
 Mutated only by feed thread.
 Read-only access for strategies / replay.
*/
class OrderBook {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

public:
    // ----- WRITE SIDE (ENGINE ONLY) -----

    void load_snapshot(const DepthSnapshot& s) {
        bids = s.bids;
        asks = s.asks;
    }

    void apply_delta(const DepthDelta& d) {
        bids = d.bids;
        asks = d.asks;
    }

    // ----- READ SIDE (STRATEGY / REPLAY) -----

    bool bids_empty() const {
        return bids.empty();
    }

    bool asks_empty() const {
        return asks.empty();
    }

    double best_bid() const {
        return bids.empty() ? 0.0 : bids.front().price;
    }

    double best_ask() const {
        return asks.empty() ? 0.0 : asks.front().price;
    }

    size_t bid_levels() const {
        return bids.size();
    }

    size_t ask_levels() const {
        return asks.size();
    }

    const std::vector<PriceLevel>& bid_side() const {
        return bids;
    }

    const std::vector<PriceLevel>& ask_side() const {
        return asks;
    }
};

}
