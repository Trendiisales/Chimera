// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Asia Exception Gate
// ═══════════════════════════════════════════════════════════════════════════════
// RULE: Asia is DEFAULT BLOCKED
// Exception: XAUUSD only, with EXTRAORDINARY signal confirmation
// Miss ONE condition → BLOCK
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>
#include <iostream>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// EXTRAORDINARY SIGNAL REQUIREMENTS
// ═══════════════════════════════════════════════════════════════════════════════

struct AsiaExtraordinarySignal {
    double edge;           // Must be ≥ 1.5× normal threshold
    double displacement;   // Must be ≥ 2.0× normal threshold
    double atr_ratio;      // Must be ≥ 1.6 (volatility expansion)
    double spread;         // Must be ≤ Asia hard cap
    bool   clean_regime;   // No chop - must be TRENDING
    bool   no_news;        // No high-impact news nearby
};

// ═══════════════════════════════════════════════════════════════════════════════
// ASIA EXCEPTION GATE
// ═══════════════════════════════════════════════════════════════════════════════

inline bool allow_asia_exception(
    const std::string& symbol,
    const AsiaExtraordinarySignal& s
) {
    // 🔒 Only Gold may pass - NAS100 is DEAD during Asia
    if (symbol != "XAUUSD")
        return false;

    // ═══════════════════════════════════════════════════════════════════════════
    // EXTRAORDINARY THRESHOLDS (ALL REQUIRED - MISS ONE = BLOCK)
    // ═══════════════════════════════════════════════════════════════════════════

    // Displacement ≥ 2.0× normal (0.35 normal → 0.70 extraordinary)
    if (s.displacement < 0.70)
        return false;

    // Edge ≥ 1.5× normal (1.2 normal → 1.8 extraordinary)
    if (s.edge < 1.8)
        return false;

    // ATR ratio ≥ 1.6 (volatility expansion required)
    if (s.atr_ratio < 1.6)
        return false;

    // Spread ≤ Asia hard cap (tighter than normal)
    if (s.spread > 0.30)
        return false;

    // Regime must be CLEAN_TREND (no chop)
    if (!s.clean_regime)
        return false;

    // No high-impact news nearby (no synthetic spikes)
    if (!s.no_news)
        return false;

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LOGGING
// ═══════════════════════════════════════════════════════════════════════════════

inline void log_asia_block(const std::string& symbol, const std::string& reason) {
    std::cerr << "[BLOCK] " << symbol << " reason=ASIA_" << reason << "\n";
}

inline void log_asia_allow(const std::string& symbol) {
    std::cerr << "[ALLOW] " << symbol << " reason=ASIA_EXTRAORDINARY\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
// ASIA RISK RULES (HARD)
// ═══════════════════════════════════════════════════════════════════════════════
// Even when allowed:
// - Symbol: XAUUSD only
// - Risk multiplier: 0.6×
// - Scaling: ❌ Disabled
// - Adds: ❌ Disabled
// - Stop movement: Standard
// - News window: Absolute block
// 
// Asia gold trades are BONUS trades, not core income.
// ═══════════════════════════════════════════════════════════════════════════════

constexpr double ASIA_RISK_MULTIPLIER = 0.6;
constexpr bool   ASIA_SCALING_ALLOWED = false;
constexpr bool   ASIA_ADDS_ALLOWED = false;

}  // namespace Alpha
