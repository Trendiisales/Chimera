#pragma once

namespace chimera {

// ---------------------------------------------------------------------------
// SpreadSizer — Dynamic position sizing based on spread quality
//
// Adjusts order size based on current spread width:
// - Tight spreads (good liquidity) → larger sizes
// - Wide spreads (poor liquidity) → smaller sizes
//
// This maintains consistent risk-adjusted exposure across varying market
// conditions and improves risk-adjusted returns.
//
// Example:
//   Reference spread: 10bps
//   Base size: 0.01
//   Current spread: 5bps → Scale: 10/5 = 2x → Size: 0.02
//   Current spread: 20bps → Scale: 10/20 = 0.5x → Size: 0.005
// ---------------------------------------------------------------------------

class SpreadSizer {
public:
    // ref_spread_bps = reference spread width in basis points
    // This is the "normal" spread for the symbol
    // max_scale = maximum size multiplier (prevents excessive sizing)
    // min_scale = minimum size multiplier (prevents zero sizing)
    explicit SpreadSizer(double ref_spread_bps, 
                        double max_scale = 3.0,
                        double min_scale = 0.25);
    
    // Calculate scaled size based on current spread
    // base_size = baseline order size
    // current_spread_bps = current bid-ask spread in basis points
    // Returns: adjusted size
    double scale_size(double base_size, double current_spread_bps) const;
    
    // Get reference spread
    double get_reference_spread() const { return ref_spread_bps_; }
    
    // Update reference spread (can be adaptive based on recent history)
    void set_reference_spread(double spread_bps) { ref_spread_bps_ = spread_bps; }

private:
    double ref_spread_bps_;  // Reference spread for normalization
    double max_scale_;       // Maximum size multiplier
    double min_scale_;       // Minimum size multiplier
};

} // namespace chimera
