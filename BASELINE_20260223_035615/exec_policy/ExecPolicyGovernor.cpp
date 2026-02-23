#include "ExecPolicyGovernor.hpp"

namespace chimera {

ExecPolicyGovernor::ExecPolicyGovernor(const ExecPolicyConfig& cfg,
                                       ExecPolicySink& sink)
    : m_cfg(cfg), m_sink(sink) {}

void ExecPolicyGovernor::on_latency(uint64_t now_ns,
                                    uint64_t exchange_rtt_ns,
                                    uint64_t queue_wait_ns) {
    m_rtt_ns = exchange_rtt_ns;
    m_queue_ns = queue_wait_ns;
    evaluate(now_ns);
}

void ExecPolicyGovernor::on_reject_rate(uint64_t now_ns,
                                        double reject_rate) {
    m_reject_rate = reject_rate;
    evaluate(now_ns);
}

void ExecPolicyGovernor::on_market_state(uint64_t now_ns,
                                         double spread_bps,
                                         double volatility_score) {
    m_spread_bps = spread_bps;
    m_volatility = volatility_score;
    evaluate(now_ns);
}

void ExecPolicyGovernor::on_exchange_instability(uint64_t now_ns,
                                                 bool unstable) {
    m_exchange_unstable = unstable;
    evaluate(now_ns);
}

void ExecPolicyGovernor::evaluate(uint64_t now_ns) {
    if (m_state.hard_kill) {
        if (now_ns - m_last_hard_kill_ns > m_cfg.hard_kill_cooldown_ns) {
            m_state.hard_kill = false;
            m_state.trading_enabled = true;
        } else {
            return;
        }
    }

    bool latency_bad =
        m_rtt_ns > m_cfg.max_rtt_ns ||
        m_queue_ns > m_cfg.max_queue_wait_ns;

    bool market_bad =
        m_spread_bps > m_cfg.max_spread_bps ||
        m_volatility > m_cfg.vol_burst_threshold;

    bool rejects_bad = m_reject_rate > m_cfg.max_reject_rate;

    if (m_exchange_unstable || (latency_bad && rejects_bad)) {
        m_state.hard_kill = true;
        m_state.trading_enabled = false;
        m_state.mode = ExecMode::DISABLED;
        m_state.size_multiplier = 0.0;
        m_last_hard_kill_ns = now_ns;
    } else if (latency_bad || market_bad) {
        m_state.trading_enabled = true;
        m_state.mode = ExecMode::TAKE_ONLY;
        m_state.size_multiplier = m_cfg.size_downscale;
    } else {
        m_state.trading_enabled = true;
        m_state.mode = ExecMode::POST_ONLY;
        m_state.size_multiplier = m_cfg.size_upscale;
    }

    m_state.last_update_ns = now_ns;
    m_sink.publish(m_state);
}

const ExecPolicyState& ExecPolicyGovernor::state() const {
    return m_state;
}

}
