#include "LatencyAttributionEngine.hpp"

namespace chimera {

LatencyAttributionEngine::LatencyAttributionEngine(LatencySink& sink)
    : m_sink(sink) {}

void LatencyAttributionEngine::on_submit(const std::string& symbol,
                                         uint64_t causal_id,
                                         uint64_t decision_ts_ns,
                                         uint64_t send_ts_ns,
                                         double price,
                                         double qty) {
    std::lock_guard<std::mutex> g(m_lock);
    LatencyRecord& rec = m_inflight[causal_id];
    rec.symbol = symbol;
    rec.causal_id = causal_id;
    rec.decision_ts_ns = decision_ts_ns;
    rec.send_ts_ns = send_ts_ns;
    rec.submit.price = price;
    rec.submit.qty = qty;
}

void LatencyAttributionEngine::on_ack(uint64_t causal_id, uint64_t ack_ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    auto it = m_inflight.find(causal_id);
    if (it != m_inflight.end()) {
        it->second.ack_ts_ns = ack_ts_ns;
    }
}

void LatencyAttributionEngine::on_fill(uint64_t causal_id,
                                       uint64_t fill_ts_ns,
                                       double fill_price,
                                       double fill_qty) {
    std::lock_guard<std::mutex> g(m_lock);
    auto it = m_inflight.find(causal_id);
    if (it == m_inflight.end()) return;

    it->second.fill_ts_ns = fill_ts_ns;
    it->second.fill.price = fill_price;
    it->second.fill.qty = fill_qty;

    m_sink.publish(it->second);
    m_inflight.erase(it);
}

void LatencyAttributionEngine::on_cancel(uint64_t causal_id,
                                         uint64_t cancel_ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    auto it = m_inflight.find(causal_id);
    if (it == m_inflight.end()) return;

    it->second.cancel_ts_ns = cancel_ts_ns;
    m_sink.publish(it->second);
    m_inflight.erase(it);
}

void LatencyAttributionEngine::on_reject(uint64_t causal_id,
                                         uint64_t reject_ts_ns) {
    std::lock_guard<std::mutex> g(m_lock);
    auto it = m_inflight.find(causal_id);
    if (it == m_inflight.end()) return;

    it->second.rejected = true;
    it->second.cancel_ts_ns = reject_ts_ns;
    m_sink.publish(it->second);
    m_inflight.erase(it);
}

}
