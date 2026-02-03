#pragma once
#include <cmath>
#include <iostream>

namespace chimera {

// ---------------------------------------------------------------------------
// TrendRegime: Detects if market is trending or range-bound.
//
// Market making strategies (like QPMM) should ONLY operate in range-bound
// markets. In trending markets, market makers bleed money because they're
// always on the wrong side (selling in uptrends, buying in downtrends).
//
// This filter uses an EMA of mid-price and measures the slope.
// If slope exceeds threshold → market is trending → disable market making.
// ---------------------------------------------------------------------------

class TrendRegime {
public:
    explicit TrendRegime(double alpha = 0.2, double trend_threshold_bps = 5.0)
        : alpha_(alpha)
        , trend_threshold_bps_(trend_threshold_bps)
        , ema_(0.0)
        , prev_ema_(0.0)
        , initialized_(false) {}

    // Returns true if market is trending (DON'T market make)
    // Returns false if market is ranging (SAFE to market make)
    bool is_trending(double current_mid) {
        if (!initialized_) {
            ema_ = current_mid;
            prev_ema_ = current_mid;
            initialized_ = true;
            return false;  // Not enough data yet, assume ranging
        }
        
        // Update EMA
        prev_ema_ = ema_;
        ema_ = alpha_ * current_mid + (1.0 - alpha_) * ema_;
        
        // Calculate slope in bps
        double slope_bps = ((ema_ - prev_ema_) / prev_ema_) * 10000.0;
        
        bool trending = std::fabs(slope_bps) > trend_threshold_bps_;
        
        if (trending) {
            std::cout << "[TREND] Detected slope=" << slope_bps 
                      << "bps (threshold=" << trend_threshold_bps_ 
                      << "bps)\n";
        }
        
        return trending;
    }

private:
    double alpha_;                  // EMA smoothing (0.2 = responsive)
    double trend_threshold_bps_;    // Trend threshold (5 bps = clear trend)
    double ema_;                    // Current EMA
    double prev_ema_;               // Previous EMA (for slope)
    bool initialized_;
};

} // namespace chimera
