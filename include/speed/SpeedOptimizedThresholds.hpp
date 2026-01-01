// =============================================================================
// SpeedOptimizedThresholds.hpp - v4.6.0 - SPEED-OPTIMISED EXECUTION GATES
// =============================================================================
// PURPOSE: Aggressive but safe numeric thresholds for HFT on co-located infra
//
// THESE VALUES ARE INTENTIONALLY AGGRESSIVE because you have:
//   ✓ Execution-layer guards (GlobalRiskGovernor)
//   ✓ Hard -$200 NZD daily stop (DailyLossGuard)
//   ✓ Ownership enforcement (EngineOwnership)
//   ✓ Instant scratch logic (time stops)
// If ANY of those were missing, these numbers would be reckless.
//
// DESIGN PRINCIPLES:
//   1. Speed helps entry timing and scratch saves - NOT more trades
//   2. Latency gates are HARD - no degraded mode above block threshold
//   3. Spread gates are RELATIVE to median (adapts to conditions)
//   4. Burst parameters are asset-specific (not one-size-fits-all)
//   5. Time stops are tight - scratch early, don't hope
//
// TIER STRUCTURE:
//   TIER 1 (Primary): NAS100 - Best microstructure, highest allocation
//   TIER 2 (Active):  US30, SPX500 - Speed-friendly, CFDEngine only
//   TIER 3 (Defensive): XAUUSD - Strict guards, post-sweep only
//   TIER 4 (Opportunistic): BTCUSDT/ETHUSDT - Kill on first loss
//
// USAGE:
//   const auto& thresholds = getSpeedThresholds("NAS100");
//   if (latency_ms > thresholds.latency_block_ms) BLOCK;
//   if (spread_bps > thresholds.getSpreadBlockBps(median)) BLOCK;
// =============================================================================
#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>

namespace Chimera {
namespace Speed {

// =============================================================================
// SYMBOL SPEED THRESHOLDS - Per-instrument tuning
// =============================================================================
struct SymbolSpeedThresholds {
    // === IDENTIFICATION ===
    char symbol[16];
    int tier;                           // 1=Primary, 2=Active, 3=Defensive, 4=Opportunistic
    
    // === LATENCY GATES (milliseconds) ===
    double latency_allow_ms;            // Full size allowed
    double latency_degrade_ms;          // Size × 0.5
    double latency_block_ms;            // Hard block
    
    // === SPREAD GATES (basis points, relative to median) ===
    double spread_allow_mult;           // e.g., 1.10 = allow up to median × 1.10
    double spread_block_mult;           // e.g., 1.20 = block above median × 1.20
    double spread_hard_cap_bps;         // Absolute cap regardless of median
    
    // === BURST DETECTION ===
    double burst_strength_min;          // Minimum burst multiplier
    int burst_age_max_ms;               // Maximum burst age in ms
    int confirm_window_min_ms;          // Minimum confirmation window
    int confirm_window_max_ms;          // Maximum confirmation window
    
    // === TIME MANAGEMENT ===
    int time_stop_ms;                   // Scratch if no continuation
    int max_hold_ms;                    // Maximum position hold time
    int no_opposing_sweep_ms;           // Require no opposing sweep in last N ms
    
    // === SIZE LIMITS ===
    double max_size_mult_vs_nas;        // Size cap relative to NAS100 allocation
    int max_entries_per_burst;          // Entries per burst event
    int max_adds;                       // Maximum position adds
    
    // === SESSION FILTER ===
    bool ny_open_only;                  // Only trade during NY open
    bool ny_continuation_ok;            // Also trade NY continuation
    bool asia_session_ok;               // Trade during Asia session
    
    // === STRATEGY FILTER ===
    bool momentum_only;                 // Only momentum/continuation trades
    bool range_fade_allowed;            // Allow mean-reversion in ranges
    bool post_sweep_required;           // Require post-sweep confirmation
    
    // === CRYPTO-SPECIFIC ===
    bool kill_on_first_loss;            // Disable after first losing trade
    double daily_max_fraction;          // Max fraction of -$200 daily limit
    
    // === DERIVED METHODS ===
    double getSpreadAllowBps(double median_bps) const {
        return std::min(median_bps * spread_allow_mult, spread_hard_cap_bps);
    }
    
    double getSpreadBlockBps(double median_bps) const {
        return std::min(median_bps * spread_block_mult, spread_hard_cap_bps * 1.2);
    }
    
    bool isLatencyOk(double lat_ms) const {
        return lat_ms <= latency_block_ms;
    }
    
    double getLatencySizeMultiplier(double lat_ms) const {
        if (lat_ms <= latency_allow_ms) return 1.0;
        if (lat_ms <= latency_degrade_ms) return 0.5;
        return 0.0;  // Blocked
    }
    
    bool isBurstValid(double strength, int age_ms) const {
        return strength >= burst_strength_min && age_ms <= burst_age_max_ms;
    }
};

// =============================================================================
// NAS100 - PRIMARY PROFIT ENGINE (TIER 1)
// =============================================================================
inline constexpr SymbolSpeedThresholds NAS100_SPEED {
    // Identification
    .symbol = "NAS100",
    .tier = 1,
    
    // Latency gates
    .latency_allow_ms = 3.5,
    .latency_degrade_ms = 5.0,
    .latency_block_ms = 5.0,            // Hard block above 5ms
    
    // Spread gates (relative to median ~0.8 bps typical)
    .spread_allow_mult = 1.10,          // Allow up to 0.88 bps
    .spread_block_mult = 1.20,          // Block above 0.96 bps
    .spread_hard_cap_bps = 3.0,         // Never trade above 3 bps
    
    // Burst detection - AGGRESSIVE
    .burst_strength_min = 1.20,
    .burst_age_max_ms = 220,
    .confirm_window_min_ms = 120,
    .confirm_window_max_ms = 180,
    
    // Time management - TIGHT
    .time_stop_ms = 300,                // Scratch if no continuation in 300ms
    .max_hold_ms = 5000,
    .no_opposing_sweep_ms = 180,
    
    // Size limits
    .max_size_mult_vs_nas = 1.0,        // Reference symbol
    .max_entries_per_burst = 1,
    .max_adds = 1,
    
    // Session filter
    .ny_open_only = false,              // Owned by Income 03:00-05:00, CFD rest
    .ny_continuation_ok = true,
    .asia_session_ok = true,            // CFD can trade Asia ranges
    
    // Strategy filter
    .momentum_only = false,
    .range_fade_allowed = false,
    .post_sweep_required = false,
    
    // Not crypto
    .kill_on_first_loss = false,
    .daily_max_fraction = 1.0
};

// =============================================================================
// US30 (DOW JONES) - ACTIVE INDEX (TIER 2)
// =============================================================================
// Strong recommendation - clean impulse moves, fake-outs resolve quickly
// Speed saves scratches when it snaps back
inline constexpr SymbolSpeedThresholds US30_SPEED {
    // Identification
    .symbol = "US30",
    .tier = 2,
    
    // Latency gates - slightly wider than NAS
    .latency_allow_ms = 3.8,
    .latency_degrade_ms = 5.0,
    .latency_block_ms = 6.0,
    
    // Spread gates (relative to median ~1.2 bps typical)
    .spread_allow_mult = 0.90 / 1.2,    // 0.90 bps absolute → ~0.75 mult
    .spread_block_mult = 1.20 / 1.2,    // 1.20 bps absolute → 1.0 mult
    .spread_hard_cap_bps = 3.5,
    
    // Burst detection - slightly less aggressive than NAS
    .burst_strength_min = 1.15,
    .burst_age_max_ms = 260,
    .confirm_window_min_ms = 140,
    .confirm_window_max_ms = 200,
    
    // Time management - wider than NAS (less whippy)
    .time_stop_ms = 350,
    .max_hold_ms = 4000,
    .no_opposing_sweep_ms = 200,
    
    // Size limits - 70% of NAS
    .max_size_mult_vs_nas = 0.70,
    .max_entries_per_burst = 1,
    .max_adds = 1,
    
    // Session filter - NY only
    .ny_open_only = true,
    .ny_continuation_ok = false,        // Exit before continuation
    .asia_session_ok = false,
    
    // Strategy filter
    .momentum_only = false,             // Same burst logic as NAS
    .range_fade_allowed = false,
    .post_sweep_required = false,
    
    // Not crypto
    .kill_on_first_loss = false,
    .daily_max_fraction = 1.0
};

// =============================================================================
// SPX500 (S&P 500) - STABILISER INDEX (TIER 2)
// =============================================================================
// Cleaner than NAS but slower - speed helps entries, not exits
inline constexpr SymbolSpeedThresholds SPX500_SPEED {
    // Identification
    .symbol = "SPX500",
    .tier = 2,
    
    // Latency gates - STRICTER than US30
    .latency_allow_ms = 3.5,
    .latency_degrade_ms = 4.5,
    .latency_block_ms = 5.0,
    
    // Spread gates - TIGHTER (very liquid)
    .spread_allow_mult = 0.60 / 0.6,    // 0.60 bps absolute
    .spread_block_mult = 0.90 / 0.6,    // 0.90 bps absolute → 1.5 mult
    .spread_hard_cap_bps = 2.5,
    
    // Burst detection - STRICTER (momentum only)
    .burst_strength_min = 1.25,         // Higher than NAS/US30
    .burst_age_max_ms = 240,
    .confirm_window_min_ms = 130,
    .confirm_window_max_ms = 180,
    
    // Time management
    .time_stop_ms = 300,
    .max_hold_ms = 5000,
    .no_opposing_sweep_ms = 180,
    
    // Size limits - 60% of NAS
    .max_size_mult_vs_nas = 0.60,
    .max_entries_per_burst = 1,
    .max_adds = 0,                      // No adds on SPX
    
    // Session filter - NY open + continuation
    .ny_open_only = true,
    .ny_continuation_ok = true,
    .asia_session_ok = false,
    
    // Strategy filter - MOMENTUM ONLY
    .momentum_only = true,              // No range fades
    .range_fade_allowed = false,
    .post_sweep_required = false,
    
    // Not crypto
    .kill_on_first_loss = false,
    .daily_max_fraction = 1.0
};

// =============================================================================
// XAUUSD (GOLD) - DEFENSIVE (TIER 3)
// =============================================================================
// Secondary, defensive - only post-sweep rejection and micro-reversal
inline constexpr SymbolSpeedThresholds XAUUSD_SPEED {
    // Identification
    .symbol = "XAUUSD",
    .tier = 3,
    
    // Latency gates - STRICT
    .latency_allow_ms = 4.0,
    .latency_degrade_ms = 4.0,          // No degraded mode - allow or block
    .latency_block_ms = 4.0,            // Block immediately above 4ms
    
    // Spread gates - VERY STRICT
    .spread_allow_mult = 0.6 / 2.5,     // 0.6 bps absolute (median ~2.5)
    .spread_block_mult = 0.6 / 2.5,     // Any spike → block
    .spread_hard_cap_bps = 2.0,         // Hard cap 2 bps
    
    // Burst detection - NOT USED (post-sweep only)
    .burst_strength_min = 99.0,         // Effectively disabled
    .burst_age_max_ms = 0,
    .confirm_window_min_ms = 0,
    .confirm_window_max_ms = 0,
    
    // Time management - ULTRA TIGHT
    .time_stop_ms = 200,                // Scratch on stall > 200ms
    .max_hold_ms = 2000,
    .no_opposing_sweep_ms = 0,          // N/A for post-sweep style
    
    // Size limits - 50% of NAS
    .max_size_mult_vs_nas = 0.50,
    .max_entries_per_burst = 1,
    .max_adds = 0,                      // No adds
    
    // Session filter
    .ny_open_only = false,              // London/NY overlap
    .ny_continuation_ok = false,
    .asia_session_ok = false,
    
    // Strategy filter - POST-SWEEP ONLY
    .momentum_only = false,
    .range_fade_allowed = false,
    .post_sweep_required = true,        // CRITICAL: Only post-sweep rejection
    
    // Not crypto
    .kill_on_first_loss = false,
    .daily_max_fraction = 1.0
};

// =============================================================================
// BTCUSDT - OPPORTUNISTIC CRYPTO (TIER 4)
// =============================================================================
// Kill on first loss, tiny fraction of daily limit
inline constexpr SymbolSpeedThresholds BTCUSDT_SPEED {
    // Identification
    .symbol = "BTCUSDT",
    .tier = 4,
    
    // Latency gates - NON-NEGOTIABLE
    .latency_allow_ms = 2.5,
    .latency_degrade_ms = 2.5,          // No degraded mode
    .latency_block_ms = 2.5,            // Hard block above 2.5ms
    
    // Spread gates
    .spread_allow_mult = 1.0,
    .spread_block_mult = 2.0,
    .spread_hard_cap_bps = 2.0,
    
    // Burst detection - STRICT
    .burst_strength_min = 1.40,         // Higher than CFDs
    .burst_age_max_ms = 120,            // Much tighter
    .confirm_window_min_ms = 60,
    .confirm_window_max_ms = 100,
    
    // Time management
    .time_stop_ms = 150,
    .max_hold_ms = 3000,
    .no_opposing_sweep_ms = 100,
    
    // Size limits - minimal
    .max_size_mult_vs_nas = 0.20,       // 20% of NAS equivalent
    .max_entries_per_burst = 1,         // One entry per burst, no scaling
    .max_adds = 0,                      // No second chance
    
    // Session filter - 24/7
    .ny_open_only = false,
    .ny_continuation_ok = true,
    .asia_session_ok = true,
    
    // Strategy filter
    .momentum_only = true,
    .range_fade_allowed = false,
    .post_sweep_required = false,
    
    // CRYPTO-SPECIFIC
    .kill_on_first_loss = true,         // Kill engine after first red trade
    .daily_max_fraction = 0.10          // Max 10% of -$200 = -$20
};

// =============================================================================
// ETHUSDT - OPPORTUNISTIC CRYPTO (TIER 4)
// =============================================================================
inline constexpr SymbolSpeedThresholds ETHUSDT_SPEED {
    // Identification
    .symbol = "ETHUSDT",
    .tier = 4,
    
    // Same as BTC
    .latency_allow_ms = 2.5,
    .latency_degrade_ms = 2.5,
    .latency_block_ms = 2.5,
    
    .spread_allow_mult = 1.0,
    .spread_block_mult = 2.0,
    .spread_hard_cap_bps = 2.5,
    
    .burst_strength_min = 1.40,
    .burst_age_max_ms = 120,
    .confirm_window_min_ms = 60,
    .confirm_window_max_ms = 100,
    
    .time_stop_ms = 150,
    .max_hold_ms = 4000,
    .no_opposing_sweep_ms = 100,
    
    .max_size_mult_vs_nas = 0.15,       // 15% of NAS equivalent
    .max_entries_per_burst = 1,
    .max_adds = 0,
    
    .ny_open_only = false,
    .ny_continuation_ok = true,
    .asia_session_ok = true,
    
    .momentum_only = true,
    .range_fade_allowed = false,
    .post_sweep_required = false,
    
    .kill_on_first_loss = true,
    .daily_max_fraction = 0.10
};

// =============================================================================
// DISABLED SYMBOLS - Speed does NOT help here
// =============================================================================
// These are explicitly disabled because speed doesn't change outcome:
//   - EURUSD, GBPUSD (FX majors - too efficient)
//   - GER40, UK100 (slow indices)
//   - Mid-session range fades
//   - Wide-stop trend strategies
//   - Any strategy needing >1-2 seconds to validate

inline constexpr SymbolSpeedThresholds DISABLED_SPEED {
    .symbol = "DISABLED",
    .tier = 99,
    .latency_allow_ms = 0.0,
    .latency_degrade_ms = 0.0,
    .latency_block_ms = 0.0,            // Everything blocked
    .spread_allow_mult = 0.0,
    .spread_block_mult = 0.0,
    .spread_hard_cap_bps = 0.0,
    .burst_strength_min = 999.0,
    .burst_age_max_ms = 0,
    .confirm_window_min_ms = 0,
    .confirm_window_max_ms = 0,
    .time_stop_ms = 0,
    .max_hold_ms = 0,
    .no_opposing_sweep_ms = 0,
    .max_size_mult_vs_nas = 0.0,
    .max_entries_per_burst = 0,
    .max_adds = 0,
    .ny_open_only = true,
    .ny_continuation_ok = false,
    .asia_session_ok = false,
    .momentum_only = true,
    .range_fade_allowed = false,
    .post_sweep_required = true,
    .kill_on_first_loss = true,
    .daily_max_fraction = 0.0
};

// =============================================================================
// THRESHOLD RESOLVER
// =============================================================================
inline const SymbolSpeedThresholds& getSpeedThresholds(const char* symbol) {
    // Indices
    if (strstr(symbol, "NAS100") || strstr(symbol, "US100")) return NAS100_SPEED;
    if (strstr(symbol, "US30")) return US30_SPEED;
    if (strstr(symbol, "SPX500") || strstr(symbol, "US500")) return SPX500_SPEED;
    
    // Metals
    if (strstr(symbol, "XAUUSD")) return XAUUSD_SPEED;
    
    // Crypto
    if (strstr(symbol, "BTCUSDT")) return BTCUSDT_SPEED;
    if (strstr(symbol, "ETHUSDT")) return ETHUSDT_SPEED;
    
    // Everything else is disabled for speed trading
    return DISABLED_SPEED;
}

inline const SymbolSpeedThresholds& getSpeedThresholds(const std::string& symbol) {
    return getSpeedThresholds(symbol.c_str());
}

// =============================================================================
// SUMMARY TABLE (for logging/config notes)
// =============================================================================
// Symbol     | Spread Allow | Spread Block | Lat Block | Burst Min | Burst Age | Size vs NAS
// -----------|--------------|--------------|-----------|-----------|-----------|------------
// NAS100     | median×1.10  | median×1.20  | 5.0 ms    | 1.20      | 220 ms    | 100%
// US30       | 0.90 bps     | 1.20 bps     | 6.0 ms    | 1.15      | 260 ms    | 70%
// SPX500     | 0.60 bps     | 0.90 bps     | 5.0 ms    | 1.25      | 240 ms    | 60%
// XAUUSD     | <0.6 bps     | <0.6 bps     | 4.0 ms    | N/A       | N/A       | 50%
// BTCUSDT    | 2.0 bps      | 4.0 bps      | 2.5 ms    | 1.40      | 120 ms    | 20%
// ETHUSDT    | 2.5 bps      | 5.0 bps      | 2.5 ms    | 1.40      | 120 ms    | 15%
// =============================================================================

} // namespace Speed
} // namespace Chimera
