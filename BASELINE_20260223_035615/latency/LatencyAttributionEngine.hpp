#pragma once
#include <unordered_map>
#include <mutex>
#include "LatencyRecord.hpp"
#include "LatencySink.hpp"

namespace chimera {

class LatencyAttributionEngine {
public:
    explicit LatencyAttributionEngine(LatencySink& sink);

    void on_submit(const std::string& symbol,
                   uint64_t causal_id,
                   uint64_t decision_ts_ns,
                   uint64_t send_ts_ns,
                   double price,
                   double qty);

    void on_ack(uint64_t causal_id, uint64_t ack_ts_ns);

    void on_fill(uint64_t causal_id,
                 uint64_t fill_ts_ns,
                 double fill_price,
                 double fill_qty);

    void on_cancel(uint64_t causal_id, uint64_t cancel_ts_ns);

    void on_reject(uint64_t causal_id, uint64_t reject_ts_ns);

private:
    LatencySink& m_sink;
    std::unordered_map<uint64_t, LatencyRecord> m_inflight;
    std::mutex m_lock;
};

}
