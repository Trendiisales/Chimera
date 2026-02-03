#include "revenue/FeeTierTracker.hpp"
#include <chrono>

namespace chimera {

FeeTierTracker::FeeTierTracker(double next_tier_volume_btc, int window_days)
    : total_volume_btc_(0.0)
    , next_tier_volume_btc_(next_tier_volume_btc)
    , window_ns_(static_cast<uint64_t>(window_days) * 24ULL * 3600ULL * 1'000'000'000ULL) {}

void FeeTierTracker::record_trade(double notional_btc) {
    using namespace std::chrono;
    uint64_t now_ns = duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
    
    std::lock_guard<std::mutex> lock(mtx_);
    
    // Remove samples older than rolling window
    expire_old_samples(now_ns);
    
    // Add new sample
    samples_.push_back({notional_btc, now_ns});
    total_volume_btc_ += notional_btc;
}

double FeeTierTracker::tier_progress() const {
    std::lock_guard<std::mutex> lock(mtx_);
    
    if (next_tier_volume_btc_ <= 0.0) {
        return 0.0;
    }
    
    double progress = total_volume_btc_ / next_tier_volume_btc_;
    return progress > 1.0 ? 1.0 : progress;
}

bool FeeTierTracker::near_tier_crossing() const {
    return tier_progress() > 0.9;
}

double FeeTierTracker::get_window_volume() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return total_volume_btc_;
}

void FeeTierTracker::set_next_tier(double volume_btc) {
    std::lock_guard<std::mutex> lock(mtx_);
    next_tier_volume_btc_ = volume_btc;
}

void FeeTierTracker::expire_old_samples(uint64_t now_ns) {
    // Remove all samples older than the rolling window
    while (!samples_.empty()) {
        uint64_t age_ns = now_ns - samples_.front().timestamp_ns;
        if (age_ns > window_ns_) {
            total_volume_btc_ -= samples_.front().notional_btc;
            samples_.pop_front();
        } else {
            break;  // Remaining samples are within window
        }
    }
}

} // namespace chimera
