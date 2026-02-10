// =============================================================================
// VwapEdgeLogic.hpp - v4.18.0 - VWAP EDGE CLASSIFICATION
// =============================================================================
// Classifies the current micro-structure edge relative to VWAP.
//
// RECLAIM:    Price near VWAP + slope accelerating = join the reclaim
// FAIL_FADE:  Price extended from VWAP = fade the failed break
// NONE:       No edge detected
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "profile/VwapTune.hpp"
#include <cmath>

namespace Chimera {

enum class EdgeType : uint8_t {
    NONE      = 0,
    RECLAIM   = 1,
    FAIL_FADE = 2
};

inline const char* edgeTypeToString(EdgeType e) {
    switch (e) {
        case EdgeType::RECLAIM:   return "RECLAIM";
        case EdgeType::FAIL_FADE: return "FAIL_FADE";
        default:                  return "NONE";
    }
}

struct VwapEdgeLogic {
    // Classify edge from price, VWAP, slope, and tune params
    static EdgeType classify(double mid,
                             double vwap,
                             double slope,
                             const VwapTune& t) {
        if (vwap <= 0.0) return EdgeType::NONE;

        double dist = std::abs(mid - vwap) / vwap;

        // RECLAIM: price near VWAP + slope shows conviction
        if (dist <= t.maxDistPct && std::abs(slope) >= t.minSlope) {
            return EdgeType::RECLAIM;
        }

        // FAIL_FADE: price extended beyond fail threshold
        if (dist >= t.failFadeDistPct) {
            return EdgeType::FAIL_FADE;
        }

        return EdgeType::NONE;
    }
};

} // namespace Chimera
