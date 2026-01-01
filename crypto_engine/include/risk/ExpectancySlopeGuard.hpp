// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/ExpectancySlopeGuard.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Catch slow edge decay before expectancy turns negative
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Non-stationarity kill switch
//
// PRINCIPLE: "Slope matters more than level"
// - Detects silent regime decay
// - Acts before expectancy flips negative
// - No human review required
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <iostream>
#include <string>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Asset-Specific Slope Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct SlopeThresholds {
    double pause_threshold;      // Below this → pause new entries
    double half_threshold;       // Below this → size ×0.5
    double decay_threshold;      // Below this → size ×0.8
    
    // Asset-specific defaults (conservative, field-tested)
    static SlopeThresholds for_crypto()  { return {-0.02, -0.015, 0.0}; }  // Fast pause
    static SlopeThresholds for_gold()    { return {-0.04, -0.02, 0.0}; }   // Earlier pause
    static SlopeThresholds for_silver()  { return {-0.03, -0.02, 0.0}; }
    static SlopeThresholds for_indices() { return {-0.05, -0.03, 0.0}; }
    static SlopeThresholds for_forex()   { return {-0.06, -0.04, 0.0}; }   // More tolerance
    
    static SlopeThresholds for_symbol(const std::string& symbol) {
        if (symbol == "BTCUSDT" || symbol == "ETHUSDT" || symbol == "SOLUSDT")
            return for_crypto();
        if (symbol == "XAUUSD")
            return for_gold();
        if (symbol == "XAGUSD")
            return for_silver();
        if (symbol == "NAS100" || symbol == "SPX500" || symbol == "US30")
            return for_indices();
        // Default to forex for EURUSD, GBPUSD, etc.
        return for_forex();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Expectancy Slope Guard
// ─────────────────────────────────────────────────────────────────────────────
struct ExpectancySlopeGuard {
    double last_expectancy = 0.0;
    double slope = 0.0;
    double slope_ema = 0.0;           // Smoothed slope for stability
    int updates = 0;
    SlopeThresholds thresholds;
    std::string symbol;
    
    static constexpr double SLOPE_ALPHA = 0.2;  // EMA smoothing for slope
    
    ExpectancySlopeGuard() = default;
    
    explicit ExpectancySlopeGuard(const std::string& sym)
        : thresholds(SlopeThresholds::for_symbol(sym))
        , symbol(sym)
    {}
    
    void update(double current_expectancy) noexcept {
        if (updates == 0) {
            last_expectancy = current_expectancy;
            updates++;
            return;
        }
        
        // Calculate raw slope
        slope = current_expectancy - last_expectancy;
        
        // EMA smooth the slope to avoid noise
        slope_ema = SLOPE_ALPHA * slope + (1.0 - SLOPE_ALPHA) * slope_ema;
        
        last_expectancy = current_expectancy;
        updates++;
        
        // Log slope changes
        if (updates % 10 == 0) {
            double mult = size_multiplier();
            if (mult < 1.0) {
                std::cout << "[SLOPE-" << symbol << "] "
                          << "slope=" << slope_ema 
                          << " mult=" << mult << "x"
                          << (is_paused() ? " PAUSED" : "")
                          << "\n";
            }
        }
    }
    
    [[nodiscard]] double size_multiplier() const noexcept {
        if (updates < 5) return 1.0;  // Need data
        
        if (slope_ema < thresholds.pause_threshold) return 0.0;   // Pause
        if (slope_ema < thresholds.half_threshold)  return 0.5;   // Half size
        if (slope_ema < thresholds.decay_threshold) return 0.8;   // Slight decay
        return 1.0;
    }
    
    [[nodiscard]] bool is_paused() const noexcept {
        return updates >= 5 && slope_ema < thresholds.pause_threshold;
    }
    
    [[nodiscard]] bool is_decaying() const noexcept {
        return updates >= 5 && slope_ema < thresholds.decay_threshold;
    }
    
    [[nodiscard]] double current_slope() const noexcept {
        return slope_ema;
    }
};

} // namespace Risk
} // namespace Chimera
