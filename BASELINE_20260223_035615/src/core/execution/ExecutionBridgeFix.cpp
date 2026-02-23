#include "ExecutionBridgeFix.hpp"

#include "../../engines/StopRunDetector.hpp"
#include "../../engines/LiquidityVacuum.hpp"
#include "../../engines/SessionBias.hpp"

#include <iostream>

namespace chimera {

ExecutionBridgeFix::ExecutionBridgeFix(double max_usd, FixAdapter* fix)
    : m_allocator(max_usd),
      m_fix(fix),
      m_client_id_seq(1) {}

void ExecutionBridgeFix::on_latency_sample(const LatencySample& s) {
    m_latency.push(s);
    m_policy.update(m_latency.state());
}

void ExecutionBridgeFix::on_execution_report(uint64_t client_order_id,
                                             uint64_t ack_ts_ns) {
    // Minimal ACK feedback loop for latency filter
    LatencySample s{};
    s.send_ts_ns = 0;     // If you track send ts externally, wire it here
    s.ack_ts_ns = ack_ts_ns;
    m_latency.push(s);
}

void ExecutionBridgeFix::on_market(double price,
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

    double edge = (intent == TradeIntent::HOLD) ? 0.0 : 1.0;
    double notional = m_allocator.allocate(edge, spread, m_latency.state());
    if (notional <= 0.0) return;

    std::string side;
    if (intent == TradeIntent::LONG) side = "BUY";
    else if (intent == TradeIntent::SHORT) side = "SELL";
    else return;

    bool post_only = (m_policy.policy() == ExecPolicy::POST_ONLY);

    uint64_t cid = m_client_id_seq++;
    uint64_t send_ts = ts_ns;

    std::cout << "[FIX] SEND " << side
              << " XAUUSD USD=" << notional
              << " POLICY=" << m_policy.policy_string()
              << " CID=" << cid << "\n";

    if (m_fix) {
        m_fix->send_new_order("XAUUSD",
                               side,
                               price,
                               notional,
                               post_only,
                               cid,
                               send_ts);
    }
}

}
