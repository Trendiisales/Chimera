#include "LossShutdownEngine.hpp"

namespace chimera {

LossShutdownEngine::LossShutdownEngine(const LossShutdownConfig& cfg)
    : m_cfg(cfg) {}

void LossShutdownEngine::on_trade(uint64_t ts_ns,
                                  double pnl,
                                  double,
                                  uint64_t) {
    if (pnl < 0 && pnl > -5.0) {
        m_small_loss_ts.push_back(ts_ns);
    }

    while (!m_small_loss_ts.empty() &&
           ts_ns - m_small_loss_ts.front() > m_cfg.window_ns) {
        m_small_loss_ts.pop_front();
    }
}

bool LossShutdownEngine::should_pause() const {
    return int(m_small_loss_ts.size()) >= m_cfg.max_small_losses;
}

}
