// =============================================================================
// VwapTune.hpp - v4.18.0 - SYMBOL-SPECIFIC VWAP TUNING
// =============================================================================
// Centralised tuning for VWAP-based edge detection per instrument.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>

namespace Chimera {

struct VwapTune {
    double maxDistPct;           // Max distance from VWAP for reclaim entry
    double minSlope;             // Min slope magnitude for acceleration filter
    double failFadeDistPct;      // Distance threshold for fail-fade trigger
    uint64_t reclaimHoldNs;      // Hold time before reclaim confirmed
    uint64_t failWindowNs;       // Window for fail-fade detection
    double supportiveImbalance;  // Imbalance threshold for support
    double collapseImbalance;    // Imbalance collapse threshold
    double edgeDecayExit;        // Exit if edge decays > this fraction
    uint64_t maxHoldNs;          // Max position hold time
    uint64_t cooldownNs;         // Post-trade cooldown
};

// ─────────────────────────────────────────────────────────────────────────────
// XAU: Wider spreads, more volatile, needs more room
// ─────────────────────────────────────────────────────────────────────────────
inline VwapTune tuneXAU() {
    return {
        .maxDistPct          = 0.0015,                  // 0.15%
        .minSlope            = 0.0000004,               // slope threshold
        .failFadeDistPct     = 0.0022,                  // fail fade trigger
        .reclaimHoldNs       = 300'000'000ULL,          // 300ms
        .failWindowNs        = 400'000'000ULL,          // 400ms
        .supportiveImbalance = 0.40,                    // imbalance support
        .collapseImbalance   = 0.25,                    // imbalance collapse
        .edgeDecayExit       = 0.45,                    // 45% edge decay
        .maxHoldNs           = 8'000'000'000ULL,        // 8s hold cap
        .cooldownNs          = 300'000'000ULL           // 300ms cooldown
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// NAS100: Tighter, faster, higher conviction needed
// ─────────────────────────────────────────────────────────────────────────────
inline VwapTune tuneNAS() {
    return {
        .maxDistPct          = 0.0012,                  // 0.12%
        .minSlope            = 0.0000006,               // higher bar
        .failFadeDistPct     = 0.0018,                  // tighter fail
        .reclaimHoldNs       = 300'000'000ULL,          // 300ms
        .failWindowNs        = 400'000'000ULL,          // 400ms
        .supportiveImbalance = 0.40,                    // imbalance support
        .collapseImbalance   = 0.25,                    // imbalance collapse
        .edgeDecayExit       = 0.45,                    // 45% edge decay
        .maxHoldNs           = 6'000'000'000ULL,        // 6s hold cap
        .cooldownNs          = 300'000'000ULL           // 300ms cooldown
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// US30: Similar to NAS but slightly looser
// ─────────────────────────────────────────────────────────────────────────────
inline VwapTune tuneUS30() {
    return {
        .maxDistPct          = 0.0014,                  // 0.14%
        .minSlope            = 0.0000005,
        .failFadeDistPct     = 0.0020,
        .reclaimHoldNs       = 300'000'000ULL,
        .failWindowNs        = 400'000'000ULL,
        .supportiveImbalance = 0.40,
        .collapseImbalance   = 0.25,
        .edgeDecayExit       = 0.45,
        .maxHoldNs           = 7'000'000'000ULL,
        .cooldownNs          = 300'000'000ULL
    };
}

} // namespace Chimera
