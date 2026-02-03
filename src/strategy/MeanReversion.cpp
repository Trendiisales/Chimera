#include "MeanReversion.hpp"
#include <cmath>

namespace chimera {

MeanReversion::MeanReversion(const std::string& symbol)
    : engine_id_("MEAN_REV_" + symbol)
    , symbol_(symbol)
    , write_idx_(0)
    , count_(0)
    , sum_(0.0)
{
    for (size_t i = 0; i < WINDOW_SIZE; ++i) {
        prices_[i] = 0.0;
    }
}

const std::string& MeanReversion::id() const {
    return engine_id_;
}

void MeanReversion::onRestore() {
    write_idx_ = 0;
    count_ = 0;
    sum_ = 0.0;
    for (size_t i = 0; i < WINDOW_SIZE; ++i) {
        prices_[i] = 0.0;
    }
}

void MeanReversion::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    if (tick.symbol != symbol_) return;

    double mid = (tick.bid + tick.ask) * 0.5;

    // Remove old value if window full
    if (count_ == WINDOW_SIZE) {
        sum_ -= prices_[write_idx_];
    } else {
        count_++;
    }

    // Add new value
    prices_[write_idx_] = mid;
    sum_ += mid;
    
    write_idx_++;
    if (write_idx_ == WINDOW_SIZE) {
        write_idx_ = 0;
    }

    // Need full window
    if (count_ < WINDOW_SIZE) return;

    double mean = sum_ / static_cast<double>(WINDOW_SIZE);
    double diff_bps = (mid - mean) / mean * 10000.0;

    double abs_pos = std::fabs(tick.position);
    if (abs_pos >= MAX_POS) return;

    if (diff_bps > 30.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = false;
        o.price = tick.bid;
        o.size = 0.01;
        out.push_back(o);
    } else if (diff_bps < -30.0) {
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = true;
        o.price = tick.ask;
        o.size = 0.01;
        out.push_back(o);
    }
}

}
