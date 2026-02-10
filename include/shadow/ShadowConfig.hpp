#pragma once

#include <string>
#include <cstdint>

namespace shadow {

/**
 * Symbol-specific execution parameters
 * Based on Documents 1-9 analysis and testing
 */
struct SymbolConfig {
    std::string symbol;
    
    // Position sizing
    double base_size;         // Size per leg
    int max_legs;             // Maximum pyramid legs
    
    // Risk parameters
    double initial_stop;      // Stop distance in points
    double target_r;          // Target R multiple
    
    // Timing
    uint64_t min_hold_ms;     // Minimum hold before exit allowed
    uint64_t cooldown_ms;     // Cooldown after exit
    
    // Signal thresholds (for ATR-normalized momentum in range [-1.0, +1.0])
    double base_entry_mom;    // Momentum required for base entry
    double pyramid_mom;       // Momentum required for adds
    double reversal_mom;      // Momentum triggering reversal exit
    
    // Pyramid controls (Document 9: MFE-based gating)
    double price_improve;     // Min price improvement for adds (points)
    double max_add_mae;       // Max MAE to allow pyramid adds (points)
    double min_mfe_leg2;      // Min MFE required before leg 2 (in R)
    double min_mfe_leg3;      // Min MFE required before leg 3 (in R)
    
    // Value anchoring (Documents 6, 9)
    double vwap_buffer;       // VWAP buffer (BUY only if price > VWAP + buffer)
    double chop_band;         // VWAP chop band (reject if |price - VWAP| < band)
    
    // Execution (Document 9: shadow friction)
    double slippage;          // Realistic fill slippage (points)
    double spread;            // Bid-ask spread (points)
};

/**
 * XAUUSD (Gold) Configuration
 * From Documents 1-9 (Document 9: battle-tested parameters)
 * - Momentum normalized to ATR (range [-1.0, +1.0])
 * - VWAP buffer: 0.30, Chop band: 0.20
 * - Max legs: 3 (Document 9)
 * - MFE gates: Leg 2 requires 0.4R, Leg 3 requires 0.7R
 */
inline SymbolConfig getXauConfig() {
    return {
        "XAUUSD",
        1.0,        // base_size
        3,          // max_legs (Document 9: battle-tested)
        1.20,       // initial_stop (stop_R)
        1.8,        // target_r
        400,        // min_hold_ms - FIX-accurate (not 2000ms!)
        15000,      // cooldown_ms (Document 9: 15s)
        0.35,       // base_entry_mom (Document 9: 0.35)
        0.55,       // pyramid_mom (Document 9: 0.55)
        0.35,       // reversal_mom
        0.40,       // price_improve
        0.50,       // max_add_mae
        0.4,        // min_mfe_leg2 (Document 9: 0.4R)
        0.7,        // min_mfe_leg3 (Document 9: 0.7R)
        0.30,       // vwap_buffer
        0.20,       // chop_band (Document 9: prevents 40-60% bad entries)
        0.12,       // slippage
        0.10        // spread (typical XAU spread)
    };
}

/**
 * XAGUSD (Silver) Configuration
 * From Documents 1-9 (Document 9: "Silver is nastier + thinner")
 * - Max legs: 2 (NO leg 3, EVER)
 * - Higher thresholds than gold
 * - Longer cooldown (25s mandatory)
 * - MFE gate: Leg 2 requires 0.5R
 * - Tighter chop band
 */
inline SymbolConfig getXagConfig() {
    return {
        "XAGUSD",
        1.0,
        2,          // max_legs (Document 9: NO leg 3, EVER)
        0.90,       // initial_stop (Document 9: stop_R = 0.9)
        1.6,        // target_r
        3000,       // min_hold_ms
        25000,      // cooldown_ms (Document 9: 25s mandatory)
        0.45,       // base_entry_mom (Document 9: 0.45)
        0.65,       // pyramid_mom (Document 9: 0.65)
        0.45,       // reversal_mom
        0.08,       // price_improve
        0.10,       // max_add_mae
        0.5,        // min_mfe_leg2 (Document 9: 0.5R)
        0.0,        // min_mfe_leg3 (N/A - max 2 legs)
        0.12,       // vwap_buffer (tighter than gold)
        0.08,       // chop_band (tighter - silver is nastier)
        0.04,       // slippage
        0.04        // spread
    };
}

/**
 * NAS100 (NASDAQ) Configuration
 * From Documents 6, 9 (Document 9: max_legs=1, NY only)
 */
inline SymbolConfig getNasConfig() {
    return {
        "NAS100",
        1.0,
        1,          // max_legs (Document 9: 1 only)
        25.0,       // initial_stop
        2.0,        // target_r
        4000,       // min_hold_ms
        7000,       // cooldown_ms
        0.30,       // base_entry_mom (Document 9: 0.30)
        0.50,       // pyramid_mom (not used if max_legs=1)
        0.50,       // reversal_mom
        8.0,        // price_improve
        10.0,       // max_add_mae
        0.0,        // min_mfe_leg2 (N/A - max 1 leg)
        0.0,        // min_mfe_leg3 (N/A)
        6.0,        // vwap_buffer
        3.0,        // chop_band
        1.5,        // slippage
        2.0         // spread
    };
}

/**
 * US30 (Dow) Configuration
 * From Documents 6, 9 (Document 9: max_legs=1, RISK-ON regime required)
 */
inline SymbolConfig getUs30Config() {
    return {
        "US30",
        1.0,
        1,          // max_legs (Document 9: 1 only)
        20.0,       // initial_stop
        1.4,        // target_r
        5000,       // min_hold_ms
        10000,      // cooldown_ms
        0.40,       // base_entry_mom (Document 9: 0.40)
        0.50,       // pyramid_mom (not used if max_legs=1)
        0.60,       // reversal_mom
        10.0,       // price_improve
        12.0,       // max_add_mae
        0.0,        // min_mfe_leg2 (N/A - max 1 leg)
        0.0,        // min_mfe_leg3 (N/A)
        8.0,        // vwap_buffer
        4.0,        // chop_band
        2.0,        // slippage
        2.5         // spread
    };
}

} // namespace shadow
