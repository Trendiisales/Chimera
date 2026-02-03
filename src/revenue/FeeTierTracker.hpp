#pragma once
#include <deque>
#include <mutex>
#include <chrono>

namespace chimera {

// ---------------------------------------------------------------------------
// FeeTierTracker — Fee optimization through volume tier management
//
// Tracks 30-day rolling trading volume to help optimize for Binance fee tiers.
// When near a tier boundary, the system can slightly bias toward maker orders
// or increase size to push over the threshold and unlock better fees.
//
// Binance USDT-M Futures fee tiers (approximate):
// - VIP 0: 0-250 BTC     → 2bps maker / 4bps taker
// - VIP 1: 250-2500 BTC  → 1.6bps maker / 4bps taker
// - VIP 2: 2500+ BTC     → 1.4bps maker / 3.5bps taker
//
// Crossing a tier can improve overall profitability by 20-40% in fees alone.
//
// ACCURACY: Uses per-trade timestamps for exact rolling window (not daily
// aggregation), ensuring accurate fee tier calculations.
// ---------------------------------------------------------------------------

struct VolumeSample {
    double notional_btc;
    uint64_t timestamp_ns;
};

class FeeTierTracker {
public:
    // next_tier_volume_btc = volume needed for next VIP tier
    // window_days = rolling window size in days (default 30)
    // Example: 250 BTC for VIP 0→1, 2500 BTC for VIP 1→2
    explicit FeeTierTracker(double next_tier_volume_btc = 250.0,
                           int window_days = 30);
    
    // Record a trade's notional value in BTC equivalent
    // Should be called on every fill
    void record_trade(double notional_btc);
    
    // Get progress toward next tier as percentage [0, 1]
    // 0.0 = just crossed tier, 1.0 = at next tier threshold
    double tier_progress() const;
    
    // Check if we're close to crossing tier (>90% progress)
    // When true, system should bias toward more volume
    bool near_tier_crossing() const;
    
    // Get current 30-day volume in BTC
    double get_window_volume() const;
    
    // Get next tier threshold
    double get_next_tier() const { return next_tier_volume_btc_; }
    
    // Set new tier target (when tier is crossed)
    void set_next_tier(double volume_btc);

private:
    mutable std::mutex mtx_;
    
    // Rolling window of trade samples with timestamps
    std::deque<VolumeSample> samples_;
    double total_volume_btc_;
    
    double next_tier_volume_btc_;  // Volume needed for next tier
    uint64_t window_ns_;           // Rolling window in nanoseconds
    
    // Remove expired samples (older than window)
    void expire_old_samples(uint64_t now_ns);
};

} // namespace chimera
