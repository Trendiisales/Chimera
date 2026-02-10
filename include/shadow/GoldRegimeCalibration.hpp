// =============================================================================
// GoldRegimeCalibration.hpp - Production Gold Trading Parameters
// =============================================================================
// Purpose: Calibrate regime gates for live gold trading
//
// PHILOSOPHY (from gold audit):
// - Gold is slower than crypto → accept lower frequency
// - Gold is mean-reverting → need strong trend confirmation
// - Gold is regime-dependent → quality gates matter
// - Gold mistakes are expensive → conservative is correct
//
// PREVIOUS ISSUE:
// - Thresholds were tuned for "A+ days only"
// - Too strict for production participation
// - System appeared "dead" during normal market conditions
//
// v4.31.12 CALIBRATION:
// - Slightly relaxed volatility floor
// - Modest confidence threshold reduction
// - Range expansion trigger adjusted
// - US30 influence (not veto)
// =============================================================================

#pragma once

namespace shadow {

// =============================================================================
// GOLD (XAUUSD) CALIBRATION
// =============================================================================

struct GoldRegimeParams {
    // VOLATILITY FLOOR (points per tick)
    // Old: 0.80 (too strict - only traded explosive days)
    // New: 0.45 (allows normal gold volatility)
    // Rationale: Gold ATR typically 0.50-1.20 during active sessions
    static constexpr double MIN_ATR_POINTS = 0.45;
    
    // ENTRY CONFIDENCE THRESHOLD
    // Old: 0.80 (A+ signals only)
    // New: 0.60 (strong signals, not perfect signals)
    // Rationale: Gold doesn't give many 0.80+ signals in range days
    static constexpr double ENTRY_CONFIDENCE = 0.60;
    
    // PYRAMID CONFIDENCE THRESHOLD
    // Old: 0.85 (almost never triggered)
    // New: 0.75 (confirms trend extension)
    // Rationale: Pyramids should be selective but possible
    static constexpr double PYRAMID_CONFIDENCE = 0.75;
    
    // MOMENTUM THRESHOLD (normalized, -1.0 to +1.0)
    // Old: 0.25 (moderate)
    // New: 0.18 (slightly more permissive)
    // Rationale: Gold moves slower - don't wait for crypto-speed momentum
    static constexpr double BASE_ENTRY_MOMENTUM = 0.18;
    
    // VWAP BUFFER (points)
    // Unchanged: 0.30 (good for gold)
    // Rationale: This prevents chop entries without being too strict
    static constexpr double VWAP_BUFFER = 0.30;
    
    // CHOP BAND (points from VWAP)
    // Unchanged: 0.50 (filters noise)
    // Rationale: If price is within 0.50pts of VWAP, it's probably ranging
    static constexpr double CHOP_BAND = 0.50;
    
    // US30 REGIME QUALITY INFLUENCE
    // This is how we use US30 regime quality (0.0-1.0):
    //
    // OLD BEHAVIOR (veto):
    //   if (us30_quality < 0.60) block_all_entries();
    //
    // NEW BEHAVIOR (influence):
    //   confidence_required = base_conf + (1.0 - us30_quality) * penalty;
    //   size_mult = us30_quality;
    //
    // Example impacts:
    //   US30 quality = 0.90 → confidence_required = 0.60, size = 1.0x
    //   US30 quality = 0.70 → confidence_required = 0.66, size = 0.7x
    //   US30 quality = 0.50 → confidence_required = 0.70, size = 0.5x
    //   US30 quality = 0.30 → confidence_required = 0.74, size = 0.3x
    //
    // This means:
    // - Good US30 regime → trade normally
    // - Poor US30 regime → need stronger gold signals, smaller size
    // - US30 NEVER fully blocks good gold setups
    static constexpr double US30_CONFIDENCE_PENALTY = 0.20;  // Max penalty to confidence
    static constexpr double US30_SIZE_INFLUENCE = 1.0;       // Direct size multiplier
    static constexpr double US30_MIN_QUALITY = 0.20;         // Below this, size = 0.2x
    
    // RANGE METRICS (for trend vs chop classification)
    // These define what counts as "expansion" for gold
    static constexpr double RANGE_FLOOR_POINTS = 25.0;    // Below = choppy
    static constexpr double RANGE_EXPANSION_POINTS = 120.0;  // Above = strong trend
    static constexpr double SWEEP_THRESHOLD_POINTS = 35.0;   // Clean move in one direction
    static constexpr double REVERT_THRESHOLD_POINTS = 18.0;  // Retrace that breaks structure
};

// =============================================================================
// SILVER (XAGUSD) CALIBRATION
// =============================================================================

struct SilverRegimeParams {
    // Silver is more volatile and mean-reverting than gold
    static constexpr double MIN_ATR_POINTS = 0.025;
    static constexpr double ENTRY_CONFIDENCE = 0.62;
    static constexpr double PYRAMID_CONFIDENCE = 0.77;
    static constexpr double BASE_ENTRY_MOMENTUM = 0.20;
    static constexpr double VWAP_BUFFER = 0.015;
    static constexpr double CHOP_BAND = 0.025;
    
    // Silver respects US30 regime more than gold
    static constexpr double US30_CONFIDENCE_PENALTY = 0.25;
    static constexpr double US30_SIZE_INFLUENCE = 1.0;
    static constexpr double US30_MIN_QUALITY = 0.25;
};

// =============================================================================
// NAS100 CALIBRATION
// =============================================================================

struct NasRegimeParams {
    // NAS is the most sensitive to US30 regime
    static constexpr double MIN_ATR_POINTS = 9.0;
    static constexpr double ENTRY_CONFIDENCE = 0.65;
    static constexpr double PYRAMID_CONFIDENCE = 0.78;
    static constexpr double BASE_ENTRY_MOMENTUM = 0.22;
    static constexpr double VWAP_BUFFER = 5.0;
    static constexpr double CHOP_BAND = 8.0;
    
    // NAS strongly influenced by US30
    static constexpr double US30_CONFIDENCE_PENALTY = 0.30;
    static constexpr double US30_SIZE_INFLUENCE = 1.0;
    static constexpr double US30_MIN_QUALITY = 0.30;
};

// =============================================================================
// CALIBRATION SUMMARY
// =============================================================================
/*
WHAT CHANGED FOR v4.31.12:

1. VOLATILITY FLOOR: 0.80 → 0.45 pts
   - Allow normal gold market conditions
   - Still filters dead zones

2. ENTRY CONFIDENCE: 0.80 → 0.60
   - From "A+ only" to "strong signals"
   - Still selective, not permissive

3. US30 REGIME: VETO → INFLUENCE
   - Poor US30 = need stronger gold signal + smaller size
   - Good US30 = trade normally
   - US30 never fully blocks anymore

WHAT DIDN'T CHANGE:

1. Risk management
   - Stop loss logic unchanged
   - Pyramid spacing unchanged
   - Daily loss limits unchanged

2. Exit discipline
   - Range-failure exits unchanged
   - Time stops unchanged
   - Trailing stops unchanged

3. Position sizing
   - Base size per symbol unchanged
   - Max legs unchanged
   - R-based scaling unchanged

EXPECTED IMPACT:

Before v4.31.12:
- ~5-10 trades/week (gold only A+ days)
- High win rate (80%+)
- Under-participation

After v4.31.12:
- ~15-25 trades/week (gold normal + good days)
- Moderate win rate (65-75%)
- Proper participation

WHAT TO WATCH:

1. If win rate drops below 60%:
   → Tighten ENTRY_CONFIDENCE back to 0.65

2. If still under-participating:
   → Check rejection stats (which gate is blocking?)
   → May need to lower MIN_ATR_POINTS to 0.40

3. If over-trading in chop:
   → Tighten CHOP_BAND to 0.60
   → Increase US30_CONFIDENCE_PENALTY to 0.25

This is CALIBRATION, not gambling.
Every change is justified by the gold audit.
*/

} // namespace shadow
