// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/execution/SpreadCaptureGuard.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Detect fake liquidity without trusting exchange stats
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Ghost liquidity immunity
//
// PRINCIPLE: "Measure actual spread capture, not fill rate"
// - Venues can lie about queue position
// - Venues can show liquidity that vanishes
// - This measures what actually happened
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <array>

namespace Chimera {
namespace Execution {

// ─────────────────────────────────────────────────────────────────────────────
// Asset-Specific Spread Capture Thresholds
// ─────────────────────────────────────────────────────────────────────────────
struct SpreadCaptureThresholds {
    double maker_off;     // Below this → disable maker
    double size_decay;    // Below this → reduce size
    
    // Asset-specific (conservative)
    static SpreadCaptureThresholds for_crypto()  { return {0.75, 0.5}; }  // Strict
    static SpreadCaptureThresholds for_gold()    { return {0.60, 0.4}; }
    static SpreadCaptureThresholds for_silver()  { return {0.70, 0.5}; }
    static SpreadCaptureThresholds for_indices() { return {0.50, 0.3}; }
    static SpreadCaptureThresholds for_forex()   { return {0.40, 0.25}; } // More tolerant
    
    static SpreadCaptureThresholds for_symbol(const std::string& symbol) {
        if (symbol == "BTCUSDT" || symbol == "ETHUSDT" || symbol == "SOLUSDT")
            return for_crypto();
        if (symbol == "XAUUSD")
            return for_gold();
        if (symbol == "XAGUSD")
            return for_silver();
        if (symbol == "NAS100" || symbol == "SPX500" || symbol == "US30")
            return for_indices();
        return for_forex();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Spread Capture Guard
// ─────────────────────────────────────────────────────────────────────────────
// Measures: EffectiveSpreadCapture = (mid_at_fill - fill_price) / quoted_spread
// For maker orders, we expect to capture ~100% of half-spread
// If capture drops, venue is lying about queue position
// ─────────────────────────────────────────────────────────────────────────────
struct SpreadCaptureGuard {
    double ema_capture = 1.0;       // EMA of capture ratio
    double min_capture = 1.0;       // Minimum observed (for debugging)
    int fill_count = 0;
    SpreadCaptureThresholds thresholds;
    std::string symbol;
    
    static constexpr double CAPTURE_ALPHA = 0.1;  // EMA smoothing
    
    SpreadCaptureGuard() = default;
    
    explicit SpreadCaptureGuard(const std::string& sym)
        : thresholds(SpreadCaptureThresholds::for_symbol(sym))
        , symbol(sym)
    {}
    
    // Call on each fill
    // expected_spread: quoted spread at order time
    // realized_spread: actual improvement we got (can be negative if slipped)
    void update(double expected_spread, double realized_spread) noexcept {
        if (expected_spread <= 0.0) return;
        
        double ratio = realized_spread / expected_spread;
        
        // Clamp ratio to reasonable bounds
        ratio = std::max(-0.5, std::min(2.0, ratio));
        
        ema_capture = CAPTURE_ALPHA * ratio + (1.0 - CAPTURE_ALPHA) * ema_capture;
        
        if (ratio < min_capture) min_capture = ratio;
        
        fill_count++;
        
        // Log significant changes
        if (fill_count % 10 == 0 || ema_capture < thresholds.size_decay) {
            std::cout << "[CAPTURE-" << symbol << "] "
                      << "ema=" << ema_capture
                      << " last=" << ratio
                      << " min=" << min_capture
                      << " mult=" << maker_multiplier() << "x"
                      << (allow_maker() ? "" : " MAKER_OFF")
                      << "\n";
        }
    }
    
    // Alternative: update from fill prices
    void update_from_fill(double mid_at_order, double fill_price, 
                          double quoted_spread, bool is_buy) noexcept {
        // For a buy, we want fill_price < mid (we got a better price)
        // For a sell, we want fill_price > mid
        double improvement;
        if (is_buy) {
            improvement = mid_at_order - fill_price;
        } else {
            improvement = fill_price - mid_at_order;
        }
        
        // Expected improvement for maker = half spread
        double expected = quoted_spread * 0.5;
        
        update(expected, improvement);
    }
    
    [[nodiscard]] double maker_multiplier() const noexcept {
        if (fill_count < 5) return 1.0;  // Need data
        
        if (ema_capture < thresholds.maker_off * 0.6) return 0.0;  // Pause
        if (ema_capture < thresholds.size_decay)      return 0.7;
        return 1.0;
    }
    
    [[nodiscard]] bool allow_maker() const noexcept {
        if (fill_count < 5) return true;  // Give benefit of doubt initially
        return ema_capture >= thresholds.maker_off;
    }
    
    [[nodiscard]] bool is_paused() const noexcept {
        return fill_count >= 5 && ema_capture < thresholds.maker_off * 0.6;
    }
    
    [[nodiscard]] double current_capture() const noexcept {
        return ema_capture;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Symbol Spread Capture Manager
// ─────────────────────────────────────────────────────────────────────────────
class SpreadCaptureManager {
public:
    static constexpr size_t MAX_SYMBOLS = 32;
    
    void init_symbol(uint16_t symbol_id, const std::string& symbol_name) noexcept {
        if (symbol_id < MAX_SYMBOLS) {
            guards_[symbol_id] = SpreadCaptureGuard(symbol_name);
        }
    }
    
    void update(uint16_t symbol_id, double expected, double realized) noexcept {
        if (symbol_id < MAX_SYMBOLS) {
            guards_[symbol_id].update(expected, realized);
        }
    }
    
    void update_from_fill(uint16_t symbol_id, double mid, double fill, 
                          double spread, bool is_buy) noexcept {
        if (symbol_id < MAX_SYMBOLS) {
            guards_[symbol_id].update_from_fill(mid, fill, spread, is_buy);
        }
    }
    
    [[nodiscard]] double maker_multiplier(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return 1.0;
        return guards_[symbol_id].maker_multiplier();
    }
    
    [[nodiscard]] bool allow_maker(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return true;
        return guards_[symbol_id].allow_maker();
    }
    
    [[nodiscard]] const SpreadCaptureGuard& get(uint16_t symbol_id) const noexcept {
        static SpreadCaptureGuard empty;
        if (symbol_id >= MAX_SYMBOLS) return empty;
        return guards_[symbol_id];
    }

private:
    std::array<SpreadCaptureGuard, MAX_SYMBOLS> guards_{};
};

} // namespace Execution
} // namespace Chimera
