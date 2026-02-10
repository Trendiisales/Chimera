// =============================================================================
// VwapPositionSizer.hpp - v4.18.0 - VWAP-CONFIDENCE POSITION SIZING
// =============================================================================
// Sizes positions based on:
//   - VWAP distance (closer = larger)
//   - Slope magnitude (steeper = larger)
//   - Base lot size scaled by these factors
//
// RULE:
//   size = base × distanceFactor × slopeFactor
//
//   distanceFactor = max(0, 1 - (distPct / maxDist))   → closer to VWAP = bigger
//   slopeFactor    = clamp(slope × 1e6, 0.5, 1.5)      → steeper slope = bigger
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace Chimera {

struct VwapPositionSizer {
    // Calculate lot size from VWAP distance and slope
    static double size(double baseLots,
                       double distPct,
                       double slope,
                       double maxDist) {
        // Closer to VWAP = more confident → bigger size
        double dFactor = std::max(0.0, 1.0 - (distPct / maxDist));

        // Steeper slope = more conviction → bigger size
        double sFactor = std::min(1.5, std::max(0.5, std::abs(slope) * 1e6));

        double qty = baseLots * dFactor * sFactor;

        // Clamp to sane range
        return std::max(0.01, std::min(qty, 5.0));
    }
};

} // namespace Chimera
