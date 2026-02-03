#include "ImpulseReversion.hpp"
#include "control/UnwindCoordinator.hpp"
#include <chrono>
#include <cmath>
#include <iostream>

namespace chimera {

extern UnwindCoordinator g_unwind_coordinator;

ImpulseReversion::ImpulseReversion(const std::string& symbol)
    : engine_id_("IMPULSE_REV_" + symbol)
    , symbol_(symbol)
    , last_submit_ns_(0)
    , head_(0)
    , count_(0)
    , window_sum_(0.0)
    , last_impulse_ns_(0)
{
    for (size_t i = 0; i < WINDOW_SIZE; ++i) {
        price_window_[i] = 0.0;
    }
}

const std::string& ImpulseReversion::id() const {
    return engine_id_;
}

void ImpulseReversion::onRestore() {
    last_submit_ns_ = 0;
    head_ = 0;
    count_ = 0;
    window_sum_ = 0.0;
    last_impulse_ns_ = 0;
    for (size_t i = 0; i < WINDOW_SIZE; ++i) {
        price_window_[i] = 0.0;
    }
}

void ImpulseReversion::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    if (tick.symbol != symbol_) return;
    
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
        return;
    }

    double bid = tick.bid;
    double ask = tick.ask;
    double mid = (bid + ask) / 2.0;
    double spread_bps = ((ask - bid) / mid) * 10000.0;
    
    if (spread_bps > 10.0) return;

    // Update window
    if (count_ < WINDOW_SIZE) {
        price_window_[count_] = mid;
        window_sum_ += mid;
        count_++;
    } else {
        window_sum_ -= price_window_[head_];
        price_window_[head_] = mid;
        window_sum_ += mid;
        head_ = (head_ + 1) % WINDOW_SIZE;
    }
    
    if (count_ < WINDOW_SIZE) return;
    
    double window_mean = window_sum_ / static_cast<double>(count_);
    double impulse_bps = ((mid - window_mean) / window_mean) * 10000.0;
    
    if (now - last_impulse_ns_ < IMPULSE_COOLDOWN_NS) return;
    
    if (std::fabs(impulse_bps) < IMPULSE_THRESHOLD_BPS) return;
    
    double inv_adj_bps = pos * INV_K * 10.0;
    double eff_edge_bps = EDGE_BPS - std::fabs(inv_adj_bps);
    
    if (eff_edge_bps <= 0.0) return;
    
    last_impulse_ns_ = now;
    
    OrderIntent o;
    o.engine_id = engine_id_;
    o.symbol = tick.symbol;
    o.size = BASE_QTY;
    
    if (impulse_bps > IMPULSE_THRESHOLD_BPS) {
        o.is_buy = false;
        o.price = bid;
        out.push_back(o);
    } else if (impulse_bps < -IMPULSE_THRESHOLD_BPS) {
        o.is_buy = true;
        o.price = ask;
        out.push_back(o);
    }
    
    last_submit_ns_ = now;
}

}
