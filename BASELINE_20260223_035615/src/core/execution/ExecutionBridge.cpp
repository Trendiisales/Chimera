#include "ExecutionBridge.hpp"

#include "../../engines/StopRunDetector.hpp"
#include "../../engines/LiquidityVacuum.hpp"
#include "../../engines/SessionBias.hpp"

#include <iostream>

namespace chimera {

ExecutionBridge::ExecutionBridge(double max_usd)
    : m_allocator(max_usd), m_last_price(0.0) {}

void ExecutionBridge::on_latency_sample(const LatencySample& s) {
    m_latency.push(s);
    m_policy.update(m_latency.state());
}

void ExecutionBridge::on_market(double price,
                                double spread,
                                double depth_top,
                                uint64_t ts_ns) {

    static StopRunDetector stoprun;
    static LiquidityVacuum vacuum;
    static SessionBias session;

    StopRunState sr = stoprun.update(price, spread, depth_top, ts_ns);
    VacuumState vac = vacuum.update(depth_top, ts_ns);
    session.update(ts_ns);

    TradeIntent intent = m_fusion.fuse(sr, vac, session.bias());

    if (m_policy.policy() == ExecPolicy::DISABLED) {
        return;
    }

    double edge = 0.0;
    if (intent != TradeIntent::HOLD) edge = 1.0;

    double notional = m_allocator.allocate(edge, spread, m_latency.state());
    if (notional <= 0.0) return;

    if (intent == TradeIntent::LONG) {
        std::cout << "[EXEC] LONG XAUUSD USD=" << notional
                  << " POLICY=" << m_policy.policy_string()
                  << " LAT=" << m_latency.state_string() << "\n";
    } else if (intent == TradeIntent::SHORT) {
        std::cout << "[EXEC] SHORT XAUUSD USD=" << notional
                  << " POLICY=" << m_policy.policy_string()
                  << " LAT=" << m_latency.state_string() << "\n";
    }
}

}
