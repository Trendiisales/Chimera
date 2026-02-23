#include "LossPatternDetector.hpp"

namespace chimera {

void LossPatternDetector::on_trade_result(uint64_t ts_ns,
                                          double pnl,
                                          double slippage_bps,
                                          uint64_t latency_ns) {
    if (pnl < 0 && pnl > -5.0) {
        m_small_loss_count++;
        if (m_small_loss_count >= 3) {
            m_events.push_back({
                RiskEventType::LOSS_CLUSTER,
                ts_ns,
                1.0
            });
            m_small_loss_count = 0;
        }
    } else {
        m_small_loss_count = 0;
    }

    if (slippage_bps > 8.0 && latency_ns > 5'000'000) {
        m_events.push_back({
            RiskEventType::LATENCY_DRIVEN,
            ts_ns,
            slippage_bps
        });
    }
}

bool LossPatternDetector::has_event() const {
    return !m_events.empty();
}

RiskEvent LossPatternDetector::pop_event() {
    RiskEvent ev = m_events.front();
    m_events.pop_front();
    return ev;
}

}
