#pragma once
#include <deque>
#include "RiskEvent.hpp"

namespace chimera {

class LossPatternDetector {
public:
    void on_trade_result(uint64_t ts_ns,
                         double pnl,
                         double slippage_bps,
                         uint64_t latency_ns);

    bool has_event() const;
    RiskEvent pop_event();

private:
    std::deque<RiskEvent> m_events;

    int m_small_loss_count = 0;
};

}
