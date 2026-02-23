#pragma once
#include <deque>
#include "LossShutdownConfig.hpp"

namespace chimera {

class LossShutdownEngine {
public:
    explicit LossShutdownEngine(const LossShutdownConfig& cfg);

    void on_trade(uint64_t ts_ns,
                  double pnl,
                  double slippage_bps,
                  uint64_t latency_ns);

    bool should_pause() const;

private:
    LossShutdownConfig m_cfg;
    std::deque<uint64_t> m_small_loss_ts;
};

}
