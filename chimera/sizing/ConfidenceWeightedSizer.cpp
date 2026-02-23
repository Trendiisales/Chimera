#include "ConfidenceWeightedSizer.hpp"
#include <algorithm>

namespace chimera {

ConfidenceWeightedSizer::ConfidenceWeightedSizer(const SizingConfig& cfg,
                                                 SizingSink& sink)
    : m_cfg(cfg), m_sink(sink) {}

void ConfidenceWeightedSizer::on_signal(uint64_t causal_id,
                                        double confidence) {
    Track& t = m_tracks[causal_id];
    t.confidence = confidence;
    t.decided = false;
}

void ConfidenceWeightedSizer::on_execution_feedback(uint64_t causal_id,
                                                    uint64_t now_ns,
                                                    uint64_t rtt_ns,
                                                    uint64_t queue_wait_ns,
                                                    double slippage_bps,
                                                    double volatility) {
    auto it = m_tracks.find(causal_id);
    if (it == m_tracks.end()) return;

    Track& t = it->second;
    if (t.decided) return;

    SizingDecision d{};
    d.causal_id = causal_id;
    d.decision_ts_ns = now_ns;
    d.confidence = t.confidence;

    if (t.confidence < m_cfg.min_confidence) {
        d.action = SizingAction::ZERO;
        d.final_size = 0.0;
    } else {
        double mult = compute_multiplier(
            t,
            rtt_ns,
            queue_wait_ns,
            slippage_bps,
            volatility
        );

        d.final_size = m_cfg.base_size * mult;

        if (mult > 1.05)
            d.action = SizingAction::SCALE_UP;
        else if (mult < 0.95)
            d.action = SizingAction::SCALE_DOWN;
        else
            d.action = SizingAction::HOLD_BASE;
    }

    t.decided = true;
    m_sink.publish(d);
}

double ConfidenceWeightedSizer::compute_multiplier(const Track& t,
                                                   uint64_t rtt_ns,
                                                   uint64_t queue_ns,
                                                   double slippage_bps,
                                                   double volatility) const {
    double mult = 1.0;

    if (t.confidence >= m_cfg.strong_confidence)
        mult *= 1.2;
    else
        mult *= std::max(0.8, t.confidence);

    if (rtt_ns <= m_cfg.max_good_rtt_ns &&
        queue_ns <= m_cfg.max_good_queue_ns)
        mult *= 1.15;
    else
        mult *= 0.85;

    if (slippage_bps <= m_cfg.max_good_slippage_bps)
        mult *= 1.1;
    else
        mult *= 0.7;

    if (volatility < m_cfg.low_vol_threshold)
        mult *= 0.85;
    else if (volatility > m_cfg.high_vol_threshold)
        mult *= 0.9;

    return std::clamp(mult,
                      m_cfg.min_scale_down,
                      m_cfg.max_scale_up);
}

}
