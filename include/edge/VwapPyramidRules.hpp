// =============================================================================
// VwapPyramidRules.hpp - v4.18.0 - EDGE-SPECIFIC PYRAMIDING
// =============================================================================
// Different pyramid rules for RECLAIM vs FAIL_FADE:
//
//   RECLAIM:   Up to 3 adds if slope holds + distance from VWAP within range
//   FAIL_FADE: Single-shot only — no pyramiding
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

namespace Chimera {

struct VwapPyramidRules {
    int    maxAdds;      // Maximum pyramid adds after initial entry
    double addDistPct;   // Min distance from VWAP for add trigger
    double minSlope;     // Min slope magnitude to allow adding
};

// ─────────────────────────────────────────────────────────────────────────────
// RECLAIM: scales into conviction
// ─────────────────────────────────────────────────────────────────────────────
inline VwapPyramidRules reclaimRules() {
    return {
        .maxAdds    = 3,          // Up to 3 adds
        .addDistPct = 0.0006,     // Must be 0.06% from VWAP
        .minSlope   = 0.0000005   // Slope must still show conviction
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// FAIL_FADE: single-shot, no pyramiding
// ─────────────────────────────────────────────────────────────────────────────
inline VwapPyramidRules failFadeRules() {
    return {
        .maxAdds    = 0,     // No adds — single shot
        .addDistPct = 0.0,
        .minSlope   = 0.0
    };
}

} // namespace Chimera
