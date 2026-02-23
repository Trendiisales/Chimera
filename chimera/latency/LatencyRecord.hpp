#pragma once
#include <cstdint>
#include <string>
#include "LatencyTypes.hpp"

namespace chimera {

struct LatencyRecord {
    std::string symbol;
    uint64_t causal_id = 0;

    uint64_t decision_ts_ns = 0;
    uint64_t send_ts_ns     = 0;
    uint64_t ack_ts_ns      = 0;
    uint64_t fill_ts_ns     = 0;
    uint64_t cancel_ts_ns   = 0;

    PriceQty submit;
    PriceQty fill;

    bool rejected = false;

    uint64_t decision_to_send_ns() const {
        return send_ts_ns > decision_ts_ns ? send_ts_ns - decision_ts_ns : 0;
    }

    uint64_t exchange_rtt_ns() const {
        return ack_ts_ns > send_ts_ns ? ack_ts_ns - send_ts_ns : 0;
    }

    uint64_t queue_wait_ns() const {
        return fill_ts_ns > ack_ts_ns ? fill_ts_ns - ack_ts_ns : 0;
    }

    uint64_t decision_to_fill_ns() const {
        return fill_ts_ns > decision_ts_ns ? fill_ts_ns - decision_ts_ns : 0;
    }

    double slippage_bps() const {
        if (submit.price <= 0.0 || fill.price <= 0.0) return 0.0;
        return (fill.price - submit.price) / submit.price * 10000.0;
    }
};

}
