// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Volatility-Adaptive Hold Extensions
// ═══════════════════════════════════════════════════════════════════════════════
// DESIGN RULE:
// - Big moves deserve patience
// - Extend holds ONLY when volatility expands
// - Not always - only when ATR ratio confirms expansion
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// ATR RATIO THRESHOLDS
// ═══════════════════════════════════════════════════════════════════════════════
// ATR ratio = current_atr / baseline_atr
// > 1.0 = expanding volatility
// < 1.0 = contracting volatility

constexpr double ATR_EXPANSION_MODERATE = 1.2;
constexpr double ATR_EXPANSION_HIGH = 1.5;
constexpr double ATR_EXPANSION_EXTREME = 2.0;

// ═══════════════════════════════════════════════════════════════════════════════
// HOLD EXTENSION BY ATR RATIO
// ═══════════════════════════════════════════════════════════════════════════════
// Returns additional R-multiple required to exit
// Higher ATR = need more R to justify exit (patience pays)

inline double hold_extension_r(double atr_ratio) {
    // Extreme expansion - maximum patience
    if (atr_ratio >= ATR_EXPANSION_EXTREME)
        return 0.45;
    
    // High expansion - significant patience
    if (atr_ratio >= ATR_EXPANSION_HIGH)
        return 0.35;
    
    // Moderate expansion - some patience
    if (atr_ratio >= ATR_EXPANSION_MODERATE)
        return 0.25;
    
    // Normal/contracting - no extension
    return 0.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HOLD TIME MULTIPLIER BY ATR
// ═══════════════════════════════════════════════════════════════════════════════
// Extends minimum hold time during volatile periods

inline double hold_time_multiplier(double atr_ratio) {
    if (atr_ratio >= ATR_EXPANSION_EXTREME)
        return 1.5;
    
    if (atr_ratio >= ATR_EXPANSION_HIGH)
        return 1.3;
    
    if (atr_ratio >= ATR_EXPANSION_MODERATE)
        return 1.15;
    
    return 1.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// STOP DISTANCE ADJUSTMENT
// ═══════════════════════════════════════════════════════════════════════════════
// Wider stops in volatile markets to avoid noise stops

inline double stop_multiplier(double atr_ratio) {
    if (atr_ratio >= ATR_EXPANSION_EXTREME)
        return 1.4;  // 40% wider stops
    
    if (atr_ratio >= ATR_EXPANSION_HIGH)
        return 1.25; // 25% wider stops
    
    if (atr_ratio >= ATR_EXPANSION_MODERATE)
        return 1.1;  // 10% wider stops
    
    return 1.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// VOLATILITY STATE CLASSIFIER
// ═══════════════════════════════════════════════════════════════════════════════

enum class VolatilityState {
    QUIET,      // ATR ratio < 0.8
    NORMAL,     // ATR ratio 0.8 - 1.2
    EXPANDING,  // ATR ratio 1.2 - 1.5
    HIGH,       // ATR ratio 1.5 - 2.0
    EXTREME     // ATR ratio > 2.0
};

inline VolatilityState classify_volatility(double atr_ratio) {
    if (atr_ratio < 0.8)
        return VolatilityState::QUIET;
    if (atr_ratio < ATR_EXPANSION_MODERATE)
        return VolatilityState::NORMAL;
    if (atr_ratio < ATR_EXPANSION_HIGH)
        return VolatilityState::EXPANDING;
    if (atr_ratio < ATR_EXPANSION_EXTREME)
        return VolatilityState::HIGH;
    return VolatilityState::EXTREME;
}

inline const char* volatility_state_str(VolatilityState vs) {
    switch (vs) {
        case VolatilityState::QUIET:     return "QUIET";
        case VolatilityState::NORMAL:    return "NORMAL";
        case VolatilityState::EXPANDING: return "EXPANDING";
        case VolatilityState::HIGH:      return "HIGH";
        case VolatilityState::EXTREME:   return "EXTREME";
    }
    return "UNKNOWN";
}

}  // namespace Alpha
