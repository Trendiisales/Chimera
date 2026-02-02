#include "QueueMarketMaker.hpp"
#include "control/UnwindCoordinator.hpp"
#include <chrono>
#include <cmath>
#include <iostream>

namespace chimera {

// Global UnwindCoordinator instance (defined in main)
extern UnwindCoordinator g_unwind_coordinator;

QueueMarketMaker::QueueMarketMaker()
    : engine_id_("QPMM")
    , last_submit_ns_(0)
    , trend_filter_(0.2, 5.0)  // alpha=0.2 (responsive), threshold=5bps
{}

const std::string& QueueMarketMaker::id() const {
    return engine_id_;
}

void QueueMarketMaker::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    // QPMM focuses on BTCUSDT - quasi-passive market making
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
    
    // Only trade when spread is reasonable (not too tight, not too wide)
    if (spread_bps < 0.5 || spread_bps > 3.0) return;

    // ---------------------------------------------------------------------------
    // TREND FILTER — market making only works in range-bound markets.
    // In trending markets, market makers bleed (selling in uptrends, etc).
    // TrendRegime uses EMA slope to detect trends.
    // If trending → return immediately (don't market make).
    // ---------------------------------------------------------------------------
    if (trend_filter_.is_trending(mid)) {
        return;  // Market is trending, don't market make
    }

    // Track EMA of mid for mean reversion detection
    auto& st = state_[tick.symbol];
    
    if (!st.initialized) {
        st.ema_mid = mid;
        st.initialized = true;
        return;
    }
    
    st.ema_mid = EMA_ALPHA * mid + (1.0 - EMA_ALPHA) * st.ema_mid;
    
    // Deviation from EMA in bps - trade when price deviates
    double dev_bps = ((mid - st.ema_mid) / st.ema_mid) * 10000.0;
    
    // Inventory skew - adjust edge based on position
    double inv_skew_bps = pos * INV_K * 10.0;
    
    // Effective edge combines base edge, deviation, and inventory
    double eff_edge_bps = EDGE_BPS + dev_bps - inv_skew_bps;
    
    // Queue position proxy from book depth
    double depth_ratio = tick.bid_size / (tick.ask_size + 1e-6);
    double queue_signal = (depth_ratio > 1.5) ? 1.0 : ((depth_ratio < 0.67) ? -1.0 : 0.0);
    
    // Generate order based on effective edge and queue signal
    if (eff_edge_bps > EDGE_BPS && queue_signal <= 0) {
        // Opportunity to sell (price above EMA + queue not stacked on bid)
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = bid;  // Maker-only: join bid
        o.size = BASE_QTY;
        out.push_back(o);
        last_submit_ns_ = now;
    }
    else if (eff_edge_bps < -EDGE_BPS && queue_signal >= 0) {
        // Opportunity to buy (price below EMA + queue not stacked on ask)
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = ask;  // Maker-only: join ask
        o.size = BASE_QTY;
        out.push_back(o);
        last_submit_ns_ = now;
    }
}

}
