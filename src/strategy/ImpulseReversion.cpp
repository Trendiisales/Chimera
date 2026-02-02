#include "ImpulseReversion.hpp"
#include "control/UnwindCoordinator.hpp"
#include <chrono>
#include <cmath>
#include <iostream>

namespace chimera {

extern UnwindCoordinator g_unwind_coordinator;

ImpulseReversion::ImpulseReversion()
    : engine_id_("IMPULSE_REV")
    , last_submit_ns_(0)
{}

const std::string& ImpulseReversion::id() const {
    return engine_id_;
}

void ImpulseReversion::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    // Scoped to BTCUSDT to avoid alpha collision with ETHFade/SOLFade
    if (tick.symbol != "BTCUSDT") return;
    
    double pos = tick.position;
    
    // UnwindCoordinator prevents fighting at position caps
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
    
    // Skip if spread too wide (low liquidity)
    if (spread_bps > 10.0) return;

    // Track mid price in rolling window
    auto& st = state_[tick.symbol];
    
    st.price_window.push_back(mid);
    st.window_sum += mid;
    
    if (st.price_window.size() > WINDOW_SIZE) {
        st.window_sum -= st.price_window.front();
        st.price_window.pop_front();
    }
    
    if (st.price_window.size() < WINDOW_SIZE) return;
    
    double window_mean = st.window_sum / static_cast<double>(st.price_window.size());
    
    // Detect impulse: current price deviates significantly from recent mean
    double impulse_bps = ((mid - window_mean) / window_mean) * 10000.0;
    
    // Check if we're in impulse cooldown
    if (now - st.last_impulse_ns < IMPULSE_COOLDOWN_NS) return;
    
    // Only trade on strong impulses beyond threshold
    if (std::fabs(impulse_bps) < IMPULSE_THRESHOLD_BPS) return;
    
    // Inventory adjustment - reduce edge when already positioned
    double inv_adj_bps = pos * INV_K * 10.0;
    
    // Effective edge after inventory adjustment
    double eff_edge_bps = EDGE_BPS - std::fabs(inv_adj_bps);
    
    if (eff_edge_bps < 5.0) return;  // Don't trade if edge too thin
    
    // Mark that we detected an impulse
    st.last_impulse_ns = now;
    
    if (impulse_bps > IMPULSE_THRESHOLD_BPS) {
        // Strong upward impulse - fade by selling
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = bid;
        o.size = BASE_QTY;
        out.push_back(o);
        last_submit_ns_ = now;
        std::cout << "[IMPULSE_REV] FADE_UP impulse=" << impulse_bps << "bps\n";
    }
    else if (impulse_bps < -IMPULSE_THRESHOLD_BPS) {
        // Strong downward impulse - fade by buying
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = ask;
        o.size = BASE_QTY;
        out.push_back(o);
        last_submit_ns_ = now;
        std::cout << "[IMPULSE_REV] FADE_DOWN impulse=" << impulse_bps << "bps\n";
    }
}

}
