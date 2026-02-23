#include "CapitalAllocator.hpp"

namespace chimera {

CapitalAllocator::CapitalAllocator(const CapitalConfig& cfg,
                                   CapitalSink& sink)
    : m_cfg(cfg), m_sink(sink) {}

void CapitalAllocator::on_pnl_update(uint64_t now_ns,
                                     double realized_pnl,
                                     double drawdown) {
    m_state.realized_pnl = realized_pnl;
    m_state.drawdown = drawdown;
    evaluate(now_ns);
}

void CapitalAllocator::evaluate(uint64_t now_ns) {
    if (m_state.drawdown <= m_cfg.max_daily_drawdown) {
        m_state.mode = RiskMode::HARD_KILL;
        m_state.global_multiplier = 0.0;
    } else if (m_state.drawdown <= m_cfg.soft_drawdown) {
        m_state.mode = RiskMode::DOWNSCALE;
        m_state.global_multiplier = m_cfg.downscale_factor;
    } else {
        if (m_last_stable_ns == 0)
            m_last_stable_ns = now_ns;

        if (now_ns - m_last_stable_ns >= m_cfg.stability_window_ns) {
            m_state.mode = RiskMode::NORMAL;
            m_state.global_multiplier = m_cfg.upscale_factor;
        }
    }

    m_state.last_update_ns = now_ns;
    m_sink.publish(m_state);
}

const CapitalState& CapitalAllocator::state() const {
    return m_state;
}

}
