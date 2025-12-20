#pragma once
#include "BinanceTypes.hpp"
#include <vector>
#include <atomic>
#include <cstdint>
#include <algorithm>

namespace binance {

/*
 Single-writer SoA order book.
 Bids sorted DESC by price.
 Asks sorted ASC by price.
 Qty == 0 => delete level (Binance semantics).
*/
class OrderBookSoA {
    std::vector<double> bid_px;
    std::vector<double> bid_qty;
    std::vector<double> ask_px;
    std::vector<double> ask_qty;

    std::atomic<uint64_t> version{0};

    static void upsert_level(std::vector<double>& px,
                             std::vector<double>& qty,
                             double price,
                             double q,
                             bool is_bid)
    {
        auto it = std::lower_bound(
            px.begin(), px.end(), price,
            [is_bid](double a, double b) {
                return is_bid ? (a > b) : (a < b);
            }
        );

        if (it != px.end() && *it == price) {
            size_t idx = static_cast<size_t>(it - px.begin());
            if (q == 0.0) {
                px.erase(px.begin() + idx);
                qty.erase(qty.begin() + idx);
            } else {
                qty[idx] = q;
            }
        } else {
            if (q == 0.0)
                return;
            size_t idx = static_cast<size_t>(it - px.begin());
            px.insert(px.begin() + idx, price);
            qty.insert(qty.begin() + idx, q);
        }
    }

public:
    void load_snapshot(const DepthSnapshot& s) {
        bid_px.clear(); bid_qty.clear();
        ask_px.clear(); ask_qty.clear();

        for (const auto& l : s.bids) {
            upsert_level(bid_px, bid_qty, l.price, l.qty, true);
        }
        for (const auto& l : s.asks) {
            upsert_level(ask_px, ask_qty, l.price, l.qty, false);
        }

        version.fetch_add(1, std::memory_order_release);
    }

    void apply_delta(const DepthDelta& d) {
        for (const auto& l : d.bids) {
            upsert_level(bid_px, bid_qty, l.price, l.qty, true);
        }
        for (const auto& l : d.asks) {
            upsert_level(ask_px, ask_qty, l.price, l.qty, false);
        }

        version.fetch_add(1, std::memory_order_release);
    }

    struct Snapshot {
        uint64_t version;
        const double* bid_px;
        const double* bid_qty;
        size_t bid_n;
        const double* ask_px;
        const double* ask_qty;
        size_t ask_n;
    };

    Snapshot snapshot() const {
        Snapshot s;
        s.version = version.load(std::memory_order_acquire);
        s.bid_px = bid_px.data();
        s.bid_qty = bid_qty.data();
        s.bid_n = bid_px.size();
        s.ask_px = ask_px.data();
        s.ask_qty = ask_qty.data();
        s.ask_n = ask_px.size();
        return s;
    }
};

}
