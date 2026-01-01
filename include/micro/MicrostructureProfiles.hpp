#pragma once
// =============================================================================
// MicrostructureProfiles.hpp v4.2.2 - Per-Symbol Microstructure Models
// =============================================================================
// Encodes symbol-specific microstructure behavior for burst trading.
// This is deterministic, explainable, and latency-safe (no ML).
//
// Each symbol gets unique parameters controlling:
//   - Burst persistence requirements
//   - Confirmation timing
//   - Displacement thresholds
//   - Snapback/mean-reversion tendency
//   - Latency sensitivity
//
// Production-grade profiles based on empirical session data.
// =============================================================================

#include <string>
#include <cstring>
#include <cstdint>

namespace Omega {

// =============================================================================
// MICROSTRUCTURE PROFILE - Per-symbol trading characteristics (COMPLETE)
// =============================================================================
struct MicrostructureProfile {
    // === BURST DETECTION ===
    double min_burst_age_ms;       // Minimum burst persistence before confirmation
    double burst_threshold_mult;   // Multiplier on burst edge threshold
    double confirm_pct;            // Confirmation percentage (0.0-1.0)
    double min_displacement_atr;   // Minimum displacement vs ATR(1s)
    double snapback_penalty;       // Mean-reversion tendency (0.0-1.0, higher = worse)
    double latency_sensitivity;    // Sensitivity to latency (0.0-1.0, higher = more sensitive)
    
    // === MARKET STRUCTURE (Required per Document 7) ===
    double tick_size;              // Minimum price increment
    double min_lot;                // Minimum contract/lot size
    double typical_spread_bps;     // Median spread in bps
    double spread_p95_bps;         // 95th percentile spread
    double depth_resilience;       // 0.0-1.0, how quickly book refills (1.0 = thick)
    double adverse_selection_rate; // 0.0-1.0, toxicity of flow (1.0 = extremely toxic)
    
    // === REGIME BEHAVIOR ===
    enum RegimeType {
        REGIME_MOMENTUM_BURST = 0,    // BTC - burst → continuation
        REGIME_CHOP_IMPULSE = 1,      // ETH - chop → impulse
        REGIME_STOP_RUN_REVERSION = 2, // XAU - stop-runs + mean reversion
        REGIME_LIQUIDITY_CLIFF = 3,   // NAS - liquidity cliffs
        REGIME_COMPRESSION_BREAKOUT = 4 // EUR - compression → expansion
    };
    int regime_type;
    
    // === ORDER TYPE PREFERENCES ===
    enum PreferredOrderType {
        ORDER_IOC_AGGRESSIVE = 0,
        ORDER_FOK_ONLY = 1,
        ORDER_POST_ONLY_PASSIVE = 2,
        ORDER_HYBRID = 3
    };
    int preferred_order;
    
    // === HOLDING CHARACTERISTICS ===
    double max_hold_ms;            // Maximum hold time before forced exit
    double target_hold_ms;         // Optimal hold time
    
    // Derived thresholds
    double getConfirmNs() const { return min_burst_age_ms * 1'000'000.0 * confirm_pct; }
    double getMinBurstNs() const { return min_burst_age_ms * 1'000'000.0; }
    bool isToxicFlow() const { return adverse_selection_rate > 0.6; }
    bool isThickBook() const { return depth_resilience > 0.7; }
};

// =============================================================================
// INDEX FUTURES - Momentum-friendly, deep liquidity
// =============================================================================

// NAS100 - Best behaved symbol, clean momentum, deep book
// Regime: liquidity cliffs, extreme toxicity near opens
inline constexpr MicrostructureProfile NAS100_PROFILE {
    // Burst detection
    .min_burst_age_ms      = 6.0,
    .burst_threshold_mult  = 1.00,
    .confirm_pct           = 0.70,
    .min_displacement_atr  = 0.15,
    .snapback_penalty      = 0.20,
    .latency_sensitivity   = 0.30,
    // Market structure
    .tick_size             = 0.25,
    .min_lot               = 1.0,
    .typical_spread_bps    = 0.8,
    .spread_p95_bps        = 2.5,
    .depth_resilience      = 0.6,
    .adverse_selection_rate = 0.7,  // High near opens
    // Regime
    .regime_type           = MicrostructureProfile::REGIME_LIQUIDITY_CLIFF,
    // Order preference
    .preferred_order       = MicrostructureProfile::ORDER_IOC_AGGRESSIVE,
    // Holding
    .max_hold_ms           = 5000.0,
    .target_hold_ms        = 500.0
};

// US100 - Alias for NAS100
inline constexpr MicrostructureProfile US100_PROFILE = NAS100_PROFILE;

// US30 - Violent but tradeable, larger tick jumps
inline constexpr MicrostructureProfile US30_PROFILE {
    .min_burst_age_ms      = 8.0,
    .burst_threshold_mult  = 1.15,
    .confirm_pct           = 0.72,
    .min_displacement_atr  = 0.20,
    .snapback_penalty      = 0.35,
    .latency_sensitivity   = 0.45,
    .tick_size             = 1.0,
    .min_lot               = 1.0,
    .typical_spread_bps    = 1.2,
    .spread_p95_bps        = 3.5,
    .depth_resilience      = 0.5,
    .adverse_selection_rate = 0.65,
    .regime_type           = MicrostructureProfile::REGIME_LIQUIDITY_CLIFF,
    .preferred_order       = MicrostructureProfile::ORDER_FOK_ONLY,
    .max_hold_ms           = 4000.0,
    .target_hold_ms        = 400.0
};

// SPX500 - Similar to NAS, slightly more reactive
inline constexpr MicrostructureProfile SPX500_PROFILE {
    .min_burst_age_ms      = 7.0,
    .burst_threshold_mult  = 1.05,
    .confirm_pct           = 0.70,
    .min_displacement_atr  = 0.18,
    .snapback_penalty      = 0.25,
    .latency_sensitivity   = 0.35,
    .tick_size             = 0.25,
    .min_lot               = 1.0,
    .typical_spread_bps    = 0.6,
    .spread_p95_bps        = 2.0,
    .depth_resilience      = 0.65,
    .adverse_selection_rate = 0.6,
    .regime_type           = MicrostructureProfile::REGIME_LIQUIDITY_CLIFF,
    .preferred_order       = MicrostructureProfile::ORDER_IOC_AGGRESSIVE,
    .max_hold_ms           = 5000.0,
    .target_hold_ms        = 500.0
};

// GER40 - European index, less liquid in US hours
inline constexpr MicrostructureProfile GER40_PROFILE {
    .min_burst_age_ms      = 9.0,
    .burst_threshold_mult  = 1.20,
    .confirm_pct           = 0.73,
    .min_displacement_atr  = 0.22,
    .snapback_penalty      = 0.40,
    .latency_sensitivity   = 0.50,
    .tick_size             = 0.5,
    .min_lot               = 1.0,
    .typical_spread_bps    = 1.5,
    .spread_p95_bps        = 4.0,
    .depth_resilience      = 0.4,
    .adverse_selection_rate = 0.55,
    .regime_type           = MicrostructureProfile::REGIME_LIQUIDITY_CLIFF,
    .preferred_order       = MicrostructureProfile::ORDER_FOK_ONLY,
    .max_hold_ms           = 6000.0,
    .target_hold_ms        = 600.0
};

// =============================================================================
// METALS - Spiky, mean-reverting, dangerous
// =============================================================================

// XAUUSD - Salvageable but strict, fake bursts everywhere
// Regime: stop-runs + mean reversion, very high toxicity
inline constexpr MicrostructureProfile XAUUSD_MICRO {
    // Burst detection
    .min_burst_age_ms      = 12.0,
    .burst_threshold_mult  = 1.35,
    .confirm_pct           = 0.75,
    .min_displacement_atr  = 0.25,
    .snapback_penalty      = 0.60,
    .latency_sensitivity   = 0.70,
    // Market structure
    .tick_size             = 0.01,
    .min_lot               = 0.01,
    .typical_spread_bps    = 2.5,
    .spread_p95_bps        = 8.0,  // Session-dependent
    .depth_resilience      = 0.3,  // Thin outside London/NY
    .adverse_selection_rate = 0.85, // Very high
    // Regime
    .regime_type           = MicrostructureProfile::REGIME_STOP_RUN_REVERSION,
    // Order preference
    .preferred_order       = MicrostructureProfile::ORDER_FOK_ONLY,
    // Holding
    .max_hold_ms           = 2000.0,  // Ultra-short
    .target_hold_ms        = 200.0
};

// XAGUSD - More volatile than gold, similar structure
inline constexpr MicrostructureProfile XAGUSD_MICRO {
    .min_burst_age_ms      = 14.0,
    .burst_threshold_mult  = 1.40,
    .confirm_pct           = 0.76,
    .min_displacement_atr  = 0.28,
    .snapback_penalty      = 0.65,
    .latency_sensitivity   = 0.75,
    .tick_size             = 0.001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 3.5,
    .spread_p95_bps        = 12.0,
    .depth_resilience      = 0.25,
    .adverse_selection_rate = 0.80,
    .regime_type           = MicrostructureProfile::REGIME_STOP_RUN_REVERSION,
    .preferred_order       = MicrostructureProfile::ORDER_FOK_ONLY,
    .max_hold_ms           = 2000.0,
    .target_hold_ms        = 200.0
};

// =============================================================================
// FX MAJORS - Structurally mean-reverting, tight spreads
// =============================================================================

// EURUSD - Extremely mean-reverting, weak bursts
// Regime: compression → expansion
inline constexpr MicrostructureProfile EURUSD_PROFILE {
    .min_burst_age_ms      = 15.0,
    .burst_threshold_mult  = 1.40,
    .confirm_pct           = 0.78,
    .min_displacement_atr  = 0.30,
    .snapback_penalty      = 0.75,
    .latency_sensitivity   = 0.80,
    .tick_size             = 0.00001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 0.8,  // Stable but deceptive
    .spread_p95_bps        = 2.0,
    .depth_resilience      = 0.5,  // Layered spoofing
    .adverse_selection_rate = 0.5,  // Medium
    .regime_type           = MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT,
    .preferred_order       = MicrostructureProfile::ORDER_HYBRID,  // passive → aggressive flip
    .max_hold_ms           = 10000.0,
    .target_hold_ms        = 1000.0
};

// GBPUSD - More impulsive than EUR, still snapback-prone
inline constexpr MicrostructureProfile GBPUSD_PROFILE {
    .min_burst_age_ms      = 14.0,
    .burst_threshold_mult  = 1.30,
    .confirm_pct           = 0.76,
    .min_displacement_atr  = 0.28,
    .snapback_penalty      = 0.70,
    .latency_sensitivity   = 0.75,
    .tick_size             = 0.00001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 1.2,
    .spread_p95_bps        = 3.0,
    .depth_resilience      = 0.45,
    .adverse_selection_rate = 0.55,
    .regime_type           = MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT,
    .preferred_order       = MicrostructureProfile::ORDER_HYBRID,
    .max_hold_ms           = 8000.0,
    .target_hold_ms        = 800.0
};

// USDJPY - Structurally hostile, exists only for explicit rejection
inline constexpr MicrostructureProfile USDJPY_PROFILE {
    .min_burst_age_ms      = 18.0,
    .burst_threshold_mult  = 1.60,
    .confirm_pct           = 0.85,
    .min_displacement_atr  = 0.40,
    .snapback_penalty      = 0.90,
    .latency_sensitivity   = 0.90,
    .tick_size             = 0.001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 1.0,
    .spread_p95_bps        = 4.0,
    .depth_resilience      = 0.3,
    .adverse_selection_rate = 0.95,  // Extremely toxic
    .regime_type           = MicrostructureProfile::REGIME_STOP_RUN_REVERSION,
    .preferred_order       = MicrostructureProfile::ORDER_FOK_ONLY,
    .max_hold_ms           = 1000.0,
    .target_hold_ms        = 100.0
};

// AUDUSD - Commodity-linked, moderate behavior
inline constexpr MicrostructureProfile AUDUSD_PROFILE {
    .min_burst_age_ms      = 14.0,
    .burst_threshold_mult  = 1.35,
    .confirm_pct           = 0.76,
    .min_displacement_atr  = 0.28,
    .snapback_penalty      = 0.70,
    .latency_sensitivity   = 0.75,
    .tick_size             = 0.00001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 1.0,
    .spread_p95_bps        = 2.5,
    .depth_resilience      = 0.45,
    .adverse_selection_rate = 0.55,
    .regime_type           = MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT,
    .preferred_order       = MicrostructureProfile::ORDER_HYBRID,
    .max_hold_ms           = 8000.0,
    .target_hold_ms        = 800.0
};

// USDCAD - Oil-linked, moderate snapback
inline constexpr MicrostructureProfile USDCAD_PROFILE {
    .min_burst_age_ms      = 14.0,
    .burst_threshold_mult  = 1.35,
    .confirm_pct           = 0.76,
    .min_displacement_atr  = 0.28,
    .snapback_penalty      = 0.70,
    .latency_sensitivity   = 0.75,
    .tick_size             = 0.00001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 1.2,
    .spread_p95_bps        = 3.0,
    .depth_resilience      = 0.4,
    .adverse_selection_rate = 0.6,
    .regime_type           = MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT,
    .preferred_order       = MicrostructureProfile::ORDER_HYBRID,
    .max_hold_ms           = 8000.0,
    .target_hold_ms        = 800.0
};

// NZDUSD - Similar to AUD
inline constexpr MicrostructureProfile NZDUSD_PROFILE = AUDUSD_PROFILE;

// USDCHF - Safe haven, moderate behavior
inline constexpr MicrostructureProfile USDCHF_PROFILE {
    .min_burst_age_ms      = 15.0,
    .burst_threshold_mult  = 1.38,
    .confirm_pct           = 0.77,
    .min_displacement_atr  = 0.29,
    .snapback_penalty      = 0.72,
    .latency_sensitivity   = 0.78,
    .tick_size             = 0.00001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 1.5,
    .spread_p95_bps        = 4.0,
    .depth_resilience      = 0.35,
    .adverse_selection_rate = 0.6,
    .regime_type           = MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT,
    .preferred_order       = MicrostructureProfile::ORDER_HYBRID,
    .max_hold_ms           = 8000.0,
    .target_hold_ms        = 800.0
};

// EURGBP - Cross pair, lower liquidity
inline constexpr MicrostructureProfile EURGBP_PROFILE {
    .min_burst_age_ms      = 16.0,
    .burst_threshold_mult  = 1.45,
    .confirm_pct           = 0.80,
    .min_displacement_atr  = 0.32,
    .snapback_penalty      = 0.78,
    .latency_sensitivity   = 0.82,
    .tick_size             = 0.00001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 2.0,
    .spread_p95_bps        = 5.0,
    .depth_resilience      = 0.3,
    .adverse_selection_rate = 0.65,
    .regime_type           = MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT,
    .preferred_order       = MicrostructureProfile::ORDER_POST_ONLY_PASSIVE,
    .max_hold_ms           = 10000.0,
    .target_hold_ms        = 1000.0
};

// =============================================================================
// CRYPTO - Continuous book, strong momentum follow-through
// =============================================================================

// BTCUSDT - Best burst asset, low FIX latency risk
// Regime: burst → continuation, high toxicity during spikes
inline constexpr MicrostructureProfile BTCUSDT_PROFILE {
    .min_burst_age_ms      = 5.0,
    .burst_threshold_mult  = 0.95,
    .confirm_pct           = 0.65,
    .min_displacement_atr  = 0.12,
    .snapback_penalty      = 0.25,
    .latency_sensitivity   = 0.25,
    .tick_size             = 0.01,
    .min_lot               = 0.001,
    .typical_spread_bps    = 0.5,  // Ultra-tight, stable
    .spread_p95_bps        = 1.5,
    .depth_resilience      = 0.8,  // Thick top-of-book, fragile deeper
    .adverse_selection_rate = 0.6,  // High during spikes
    .regime_type           = MicrostructureProfile::REGIME_MOMENTUM_BURST,
    .preferred_order       = MicrostructureProfile::ORDER_IOC_AGGRESSIVE,
    .max_hold_ms           = 3000.0,  // Milliseconds-seconds
    .target_hold_ms        = 300.0
};

// ETHUSDT - Slightly noisier than BTC
// Regime: chop → impulse
inline constexpr MicrostructureProfile ETHUSDT_PROFILE {
    .min_burst_age_ms      = 6.0,
    .burst_threshold_mult  = 1.05,
    .confirm_pct           = 0.68,
    .min_displacement_atr  = 0.15,
    .snapback_penalty      = 0.35,
    .latency_sensitivity   = 0.30,
    .tick_size             = 0.01,
    .min_lot               = 0.001,
    .typical_spread_bps    = 0.7,  // Slightly wider than BTC
    .spread_p95_bps        = 2.0,
    .depth_resilience      = 0.6,  // Uneven, more spoofing
    .adverse_selection_rate = 0.55, // Medium-high
    .regime_type           = MicrostructureProfile::REGIME_CHOP_IMPULSE,
    .preferred_order       = MicrostructureProfile::ORDER_HYBRID,  // IOC + post-only fallback
    .max_hold_ms           = 4000.0,
    .target_hold_ms        = 400.0
};

// SOLUSDT - Fast but whippy
inline constexpr MicrostructureProfile SOLUSDT_PROFILE {
    .min_burst_age_ms      = 7.0,
    .burst_threshold_mult  = 1.15,
    .confirm_pct           = 0.70,
    .min_displacement_atr  = 0.18,
    .snapback_penalty      = 0.45,
    .latency_sensitivity   = 0.35,
    .tick_size             = 0.001,
    .min_lot               = 0.01,
    .typical_spread_bps    = 1.0,
    .spread_p95_bps        = 3.0,
    .depth_resilience      = 0.5,
    .adverse_selection_rate = 0.5,
    .regime_type           = MicrostructureProfile::REGIME_CHOP_IMPULSE,
    .preferred_order       = MicrostructureProfile::ORDER_IOC_AGGRESSIVE,
    .max_hold_ms           = 3000.0,
    .target_hold_ms        = 300.0
};

// AVAXUSDT - Similar to SOL
inline constexpr MicrostructureProfile AVAXUSDT_PROFILE = SOLUSDT_PROFILE;

// LINKUSDT - More stable than SOL
inline constexpr MicrostructureProfile LINKUSDT_PROFILE {
    .min_burst_age_ms      = 6.5,
    .burst_threshold_mult  = 1.10,
    .confirm_pct           = 0.68,
    .min_displacement_atr  = 0.16,
    .snapback_penalty      = 0.40,
    .latency_sensitivity   = 0.32,
    .tick_size             = 0.001,
    .min_lot               = 0.1,
    .typical_spread_bps    = 1.2,
    .spread_p95_bps        = 3.0,
    .depth_resilience      = 0.55,
    .adverse_selection_rate = 0.45,
    .regime_type           = MicrostructureProfile::REGIME_MOMENTUM_BURST,
    .preferred_order       = MicrostructureProfile::ORDER_IOC_AGGRESSIVE,
    .max_hold_ms           = 4000.0,
    .target_hold_ms        = 400.0
};

// OPUSDT - Layer 2, more volatile
inline constexpr MicrostructureProfile OPUSDT_PROFILE {
    .min_burst_age_ms      = 7.5,
    .burst_threshold_mult  = 1.20,
    .confirm_pct           = 0.72,
    .min_displacement_atr  = 0.20,
    .snapback_penalty      = 0.50,
    .latency_sensitivity   = 0.38,
    .tick_size             = 0.0001,
    .min_lot               = 0.1,
    .typical_spread_bps    = 1.5,
    .spread_p95_bps        = 4.0,
    .depth_resilience      = 0.4,
    .adverse_selection_rate = 0.55,
    .regime_type           = MicrostructureProfile::REGIME_CHOP_IMPULSE,
    .preferred_order       = MicrostructureProfile::ORDER_IOC_AGGRESSIVE,
    .max_hold_ms           = 3000.0,
    .target_hold_ms        = 300.0
};

// ARBUSDT - Similar to OP
inline constexpr MicrostructureProfile ARBUSDT_PROFILE = OPUSDT_PROFILE;

// =============================================================================
// PROFILE RESOLVER - Returns reference to appropriate profile
// =============================================================================
inline const MicrostructureProfile& GetMicrostructureProfile(const char* symbol) {
    // Indices
    if (strstr(symbol, "NAS100") || strstr(symbol, "US100")) return NAS100_PROFILE;
    if (strstr(symbol, "US30")) return US30_PROFILE;
    if (strstr(symbol, "SPX500") || strstr(symbol, "US500")) return SPX500_PROFILE;
    if (strstr(symbol, "GER40") || strstr(symbol, "DAX")) return GER40_PROFILE;
    
    // Metals
    if (strstr(symbol, "XAUUSD")) return XAUUSD_MICRO;
    if (strstr(symbol, "XAGUSD")) return XAGUSD_MICRO;
    
    // FX Majors
    if (strstr(symbol, "EURUSD")) return EURUSD_PROFILE;
    if (strstr(symbol, "GBPUSD")) return GBPUSD_PROFILE;
    if (strstr(symbol, "USDJPY")) return USDJPY_PROFILE;
    if (strstr(symbol, "AUDUSD")) return AUDUSD_PROFILE;
    if (strstr(symbol, "USDCAD")) return USDCAD_PROFILE;
    if (strstr(symbol, "NZDUSD")) return NZDUSD_PROFILE;
    if (strstr(symbol, "USDCHF")) return USDCHF_PROFILE;
    if (strstr(symbol, "EURGBP")) return EURGBP_PROFILE;
    
    // Crypto
    if (strstr(symbol, "BTCUSDT")) return BTCUSDT_PROFILE;
    if (strstr(symbol, "ETHUSDT")) return ETHUSDT_PROFILE;
    if (strstr(symbol, "SOLUSDT")) return SOLUSDT_PROFILE;
    if (strstr(symbol, "AVAXUSDT")) return AVAXUSDT_PROFILE;
    if (strstr(symbol, "LINKUSDT")) return LINKUSDT_PROFILE;
    if (strstr(symbol, "OPUSDT")) return OPUSDT_PROFILE;
    if (strstr(symbol, "ARBUSDT")) return ARBUSDT_PROFILE;
    
    // Default: NAS100 (momentum-friendly, conservative)
    return NAS100_PROFILE;
}

// Overload for std::string
inline const MicrostructureProfile& GetMicrostructureProfile(const std::string& symbol) {
    return GetMicrostructureProfile(symbol.c_str());
}

} // namespace Omega
