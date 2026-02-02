#include "forensics/EdgeAttribution.hpp"
#include "runtime/Context.hpp"
#include "execution/CancelFederation.hpp"
#include "control/PnLGovernor.hpp"
#include <iostream>

using namespace chimera;

EdgeAttribution::EdgeAttribution(Context& ctx)
    : ctx_(ctx) {}

void EdgeAttribution::on_submit(
    const std::string& order_id,
    const std::string& engine_id,
    double predicted_edge_bps,
    double queue_pos)
{
    pending_[order_id] = Pending{engine_id, predicted_edge_bps, queue_pos};
}

void EdgeAttribution::on_fill(
    const std::string& order_id,
    double realized_pnl_bps,
    double latency_us)
{
    auto it = pending_.find(order_id);
    if (it == pending_.end()) return;

    const Pending& p = it->second;
    EngineStats&   s = engines_[p.engine_id];

    double leak          = p.predicted_edge_bps - realized_pnl_bps;
    double latency_factor = latency_us * leak;  // positive leak * high latency = toxic

    s.ewma_edge_leak     = (1.0 - alpha_) * s.ewma_edge_leak     + alpha_ * leak;
    s.ewma_latency_sens  = (1.0 - alpha_) * s.ewma_latency_sens  + alpha_ * latency_factor;

    bool win = realized_pnl_bps > 0.0;
    s.win_rate = (s.win_rate * static_cast<double>(s.trades) +
                  (win ? 1.0 : 0.0)) /
                 static_cast<double>(s.trades + 1);

    s.trades++;

    // ---------------------------------------------------------------------------
    // Kill check: edge leak OR latency sensitivity breaches threshold.
    // PnL Governor blocks engine (prevents future submits from this engine).
    // ---------------------------------------------------------------------------
    if (s.trades >= 5 &&  // don't kill on noise from <5 fills
        (s.ewma_edge_leak > max_edge_leak_bps_ ||
         s.ewma_latency_sens > max_latency_sens_))
    {
        std::cerr << "[EDGE] ENGINE KILLED: " << p.engine_id
                  << " leak=" << s.ewma_edge_leak
                  << " lat_sens=" << s.ewma_latency_sens
                  << " trades=" << s.trades << "\n";

        ctx_.pnl.block_engine(p.engine_id);
        // NOTE: cancel_fed is NOT fired here. Single engine kill is per-engine,
        // not a system event. PnL.block_engine() gates all future submits from
        // this engine. In-flight orders resolve via normal lifecycle.
        // cancel_fed is reserved for: drift, portfolio DD, multi-desk regime,
        // queue TTL — events where the SYSTEM is unsafe, not just one engine.
    }

    pending_.erase(it);
}

void EdgeAttribution::on_cancel(const std::string& order_id) {
    pending_.erase(order_id);
}

const EdgeAttribution::EngineStats&
EdgeAttribution::stats(const std::string& engine_id) const {
    static EngineStats empty{};
    auto it = engines_.find(engine_id);
    if (it == engines_.end()) return empty;
    return it->second;
}
