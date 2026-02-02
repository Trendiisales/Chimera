#include "SOLFade.hpp"
#include "control/UnwindCoordinator.hpp"
#include <chrono>
#include <cmath>
#include <iostream>

namespace chimera {

extern UnwindCoordinator g_unwind_coordinator;

SOLFade::SOLFade()
    : engine_id_("SOL_FADE")
    , last_submit_ns_(0)
{}

const std::string& SOLFade::id() const {
    return engine_id_;
}

void SOLFade::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    if (tick.symbol != "SOLUSDT") return;
    
    double pos = tick.position;
    
    g_unwind_coordinator.try_lock(tick.symbol, engine_id_, pos);
    if (!g_unwind_coordinator.can_trade(tick.symbol, engine_id_)) {
        return;
    }
    g_unwind_coordinator.check_release(tick.symbol, pos);

    using namespace std::chrono;
    uint64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    
    if (now - last_submit_ns_ < THROTTLE_NS) return;

    double abs_pos = std::fabs(pos);
    if (abs_pos >= MAX_POS) {
        return;  // At cap
    }

    double bid = tick.bid;
    double ask = tick.ask;
    double mid = (bid + ask) / 2.0;
    double spread_bps = ((ask - bid) / mid) * 10000.0;
    
    if (spread_bps > 20.0) return;

    // OFI proxy - SOL needs faster reaction
    double ofi = (tick.bid_size - tick.ask_size) / (tick.bid_size + tick.ask_size + 1e-6);
    double edge_bps = (ofi * 14.0) - (pos * INV_K * 12.0);

    if (std::fabs(edge_bps) < EDGE_BPS) return;

    OrderIntent o;
    o.engine_id = engine_id_;
    o.symbol = tick.symbol;
    o.is_buy = (edge_bps < 0);
    o.price = o.is_buy ? bid : ask;
    o.size = BASE_QTY;

    out.push_back(o);
    last_submit_ns_ = now;
}

}
