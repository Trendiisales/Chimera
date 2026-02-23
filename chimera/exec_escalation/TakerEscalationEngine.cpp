#include "TakerEscalationEngine.hpp"

namespace chimera {

TakerEscalationEngine::TakerEscalationEngine(const EscalationConfig& cfg,
                                             EscalationSink& sink)
    : m_cfg(cfg), m_sink(sink) {}

void TakerEscalationEngine::on_signal(uint64_t causal_id,
                                      uint64_t signal_ts_ns,
                                      double confidence) {
    Track& t = m_tracks[causal_id];
    t.signal_ts = signal_ts_ns;
    t.confidence = confidence;
    t.decided = false;
}

void TakerEscalationEngine::on_execution_state(uint64_t causal_id,
                                               uint64_t now_ns,
                                               uint64_t queue_wait_ns,
                                               uint64_t rtt_ns,
                                               double volatility) {
    auto it = m_tracks.find(causal_id);
    if (it == m_tracks.end()) return;

    Track& t = it->second;
    if (t.decided) return;

    uint64_t since_signal = now_ns - t.signal_ts;

    EscalationDecision d{};
    d.causal_id = causal_id;
    d.decision_ts_ns = now_ns;
    d.confidence = t.confidence;

    if (t.confidence < m_cfg.min_signal_confidence) {
        d.action = EscalationAction::ABORT_TRADE;
    } else if (since_signal < m_cfg.min_confirm_ns) {
        d.action = EscalationAction::STAY_POST_ONLY;
    } else if (
        queue_wait_ns > m_cfg.max_queue_wait_ns &&
        rtt_ns < m_cfg.max_rtt_ns &&
        volatility >= m_cfg.min_volatility
    ) {
        d.action = EscalationAction::ESCALATE_TO_TAKER;
    } else if (since_signal > m_cfg.max_total_wait_ns) {
        d.action = EscalationAction::ABORT_TRADE;
    } else {
        d.action = EscalationAction::STAY_POST_ONLY;
    }

    t.decided = true;
    m_sink.publish(d);
}

}
