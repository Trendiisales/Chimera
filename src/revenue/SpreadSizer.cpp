#include "revenue/SpreadSizer.hpp"
#include <algorithm>
#include <cmath>

namespace chimera {

SpreadSizer::SpreadSizer(double ref_spread_bps, double max_scale, double min_scale)
    : ref_spread_bps_(ref_spread_bps)
    , max_scale_(max_scale)
    , min_scale_(min_scale) {}

double SpreadSizer::scale_size(double base_size, double current_spread_bps) const {
    // Handle edge cases
    if (current_spread_bps <= 0.0 || ref_spread_bps_ <= 0.0) {
        return base_size;  // No scaling if spread data is invalid
    }
    
    // Calculate scale factor: tighter spread = larger size
    // If current spread is half of reference, double the size
    double scale = ref_spread_bps_ / current_spread_bps;
    
    // Clamp to min/max bounds to prevent extreme sizing
    scale = std::max(min_scale_, std::min(max_scale_, scale));
    
    return base_size * scale;
}

} // namespace chimera
