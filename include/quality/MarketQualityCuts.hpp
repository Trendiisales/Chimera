// =============================================================================
// MarketQualityCuts.hpp - v4.10.0 - STRUCTURAL FILTER LAYER
// =============================================================================
// PURPOSE: Remove ~45% of losing trades through structural filtering.
//          NOT curve fitting, NOT parameter optimization.
//          Pure market structure quality gates.
//
// CUTS APPLIED:
//   1. ATR Regime Filter - Skip high volatility trend days
//   2. Opening Range Percentile - Skip dead/explosive opens
//   3. Failed Break Timing - Skip slow bleed reversions
//   4. Compression Quality - Skip fake consolidations
//   5. Asia Balance (Gold/FX) - Skip trend gold days
//   6. FX Sweep Timing - Skip late fake sweeps
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-06
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>
#include <numeric>

namespace Chimera {

// =============================================================================
// MARKET QUALITY CUTS - DROP-IN FILTER FOR ALL ENGINES
// =============================================================================
class MarketQualityCuts {
public:
    // =========================================================================
    // CONFIG
    // =========================================================================
    struct Config {
        // ATR regime filter
        double atr_expansion_max = 1.25;        // Block if ATR > median * 1.25
        size_t atr_history_min = 30;            // Minimum history for ATR filter
        
        // Opening range percentile
        double or_percentile_min = 0.30;        // Minimum OR percentile
        double or_percentile_max = 0.75;        // Maximum OR percentile
        size_t or_history_size = 60;            // Rolling history size
        
        // Failed break timing
        uint32_t failed_break_max_bars = 3;     // Max bars since break for E2
        
        // Compression quality
        double compression_ratio = 0.6;         // StdDev(5) < StdDev(20) * ratio
        
        // Asia balance (Gold/FX)
        double asia_balance_threshold = 0.20;   // Close within 20% of midpoint
        
        // FX sweep timing
        uint32_t fx_sweep_max_minutes = 45;     // Max minutes from London open
        uint32_t fx_snapback_max_bars = 3;      // Max bars to re-entry
    };
    
    MarketQualityCuts() = default;
    
    void setConfig(const Config& cfg) { config_ = cfg; }
    const Config& config() const { return config_; }
    
    // =========================================================================
    // GLOBAL VOLATILITY CUT (ATR REGIME)
    // Skip trading when volatility is elevated (news, trend days)
    // =========================================================================
    [[nodiscard]] bool allowDay(double atr_today) {
        // Always allow if insufficient history
        if (atr_history_count_ < config_.atr_history_min) {
            pushATR(atr_today);
            return true;
        }
        
        double atr_med = medianATR();
        pushATR(atr_today);
        
        // Block if ATR exceeds threshold
        if (atr_today > atr_med * config_.atr_expansion_max) {
            printf("[CUTS] ATR_REGIME_BLOCK: today=%.4f median=%.4f ratio=%.2f > %.2f\n",
                   atr_today, atr_med, atr_today / atr_med, config_.atr_expansion_max);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // OPENING RANGE PERCENTILE CUT
    // Only trade middle-regime OR (not dead, not explosive)
    // =========================================================================
    [[nodiscard]] bool allowOpeningRange(double or_range) {
        pushOR(or_range);
        
        // Always allow if insufficient history
        if (or_history_count_ < config_.or_history_size / 2) {
            return true;
        }
        
        double pct = computeORPercentile(or_range);
        
        if (pct < config_.or_percentile_min || pct > config_.or_percentile_max) {
            printf("[CUTS] OR_PERCENTILE_BLOCK: range=%.2f percentile=%.2f (allowed: %.2f-%.2f)\n",
                   or_range, pct, config_.or_percentile_min, config_.or_percentile_max);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // FAILED BREAK TIMING CUT
    // If reversion doesn't happen quickly, it usually never happens
    // =========================================================================
    [[nodiscard]] bool allowFailedBreak(uint32_t bars_since_break) const {
        if (bars_since_break > config_.failed_break_max_bars) {
            printf("[CUTS] FAILED_BREAK_TIMING_BLOCK: bars=%u > max=%u\n",
                   bars_since_break, config_.failed_break_max_bars);
            return false;
        }
        return true;
    }
    
    // =========================================================================
    // COMPRESSION QUALITY CUT
    // Require real volatility contraction, not fake consolidation
    // =========================================================================
    [[nodiscard]] bool compressionValid(const double* last_5, size_t n5,
                                         const double* last_20, size_t n20) const {
        if (n5 < 2 || n20 < 5) return true;  // Insufficient data
        
        double stddev_5 = computeStdDev(last_5, n5);
        double stddev_20 = computeStdDev(last_20, n20);
        
        if (stddev_20 <= 0.0) return true;
        
        if (stddev_5 >= stddev_20 * config_.compression_ratio) {
            printf("[CUTS] COMPRESSION_QUALITY_BLOCK: stddev5=%.4f stddev20=%.4f ratio=%.2f >= %.2f\n",
                   stddev_5, stddev_20, stddev_5 / stddev_20, config_.compression_ratio);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // ASIA BALANCE CUT (Gold/FX)
    // Skip trading when Asia already trends
    // =========================================================================
    [[nodiscard]] bool asiaBalanced(double asia_high, double asia_low, double asia_close) const {
        if (asia_high <= asia_low) return true;  // Invalid data
        
        double mid = (asia_high + asia_low) / 2.0;
        double range = asia_high - asia_low;
        double dist = std::abs(asia_close - mid);
        
        if (dist > range * config_.asia_balance_threshold) {
            printf("[CUTS] ASIA_BALANCE_BLOCK: close=%.4f mid=%.4f dist=%.4f > threshold=%.4f\n",
                   asia_close, mid, dist, range * config_.asia_balance_threshold);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // FX LONDON SWEEP TIMING CUT
    // Late sweeps are usually fake
    // =========================================================================
    [[nodiscard]] bool allowFXSweep(uint32_t minutes_from_london_open) const {
        if (minutes_from_london_open > config_.fx_sweep_max_minutes) {
            printf("[CUTS] FX_SWEEP_TIMING_BLOCK: minutes=%u > max=%u\n",
                   minutes_from_london_open, config_.fx_sweep_max_minutes);
            return false;
        }
        return true;
    }
    
    // =========================================================================
    // FX SNAPBACK SPEED CUT
    // =========================================================================
    [[nodiscard]] bool allowSnapback(uint32_t bars_to_reentry) const {
        if (bars_to_reentry > config_.fx_snapback_max_bars) {
            printf("[CUTS] FX_SNAPBACK_BLOCK: bars=%u > max=%u\n",
                   bars_to_reentry, config_.fx_snapback_max_bars);
            return false;
        }
        return true;
    }
    
    // =========================================================================
    // ASIA RANGE PERCENTILE (for FX)
    // =========================================================================
    [[nodiscard]] bool asiaRangeQuality(double asia_range, double percentile_threshold = 0.60) {
        pushAsiaRange(asia_range);
        
        if (asia_range_count_ < 20) return true;  // Insufficient history
        
        double pct = computeAsiaRangePercentile(asia_range);
        
        if (pct < percentile_threshold) {
            printf("[CUTS] ASIA_RANGE_BLOCK: range=%.4f percentile=%.2f < %.2f\n",
                   asia_range, pct, percentile_threshold);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // RESET (call at start of day)
    // =========================================================================
    void resetDaily() {
        // Don't reset history - it accumulates over time
        // Only reset daily tracking if needed
    }
    
    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    void printStatus() const {
        printf("[CUTS] Status:\n");
        printf("  ATR history: %zu samples, median=%.4f\n", 
               atr_history_count_, atr_history_count_ > 0 ? medianATR() : 0.0);
        printf("  OR history: %zu samples\n", or_history_count_);
        printf("  Asia range history: %zu samples\n", asia_range_count_);
    }
    
private:
    Config config_;
    
    // ATR history (rolling)
    static constexpr size_t MAX_ATR_HISTORY = 60;
    std::array<double, MAX_ATR_HISTORY> atr_history_{};
    size_t atr_history_idx_ = 0;
    size_t atr_history_count_ = 0;
    
    // OR history (rolling)
    static constexpr size_t MAX_OR_HISTORY = 60;
    std::array<double, MAX_OR_HISTORY> or_history_{};
    size_t or_history_idx_ = 0;
    size_t or_history_count_ = 0;
    
    // Asia range history (rolling)
    static constexpr size_t MAX_ASIA_HISTORY = 30;
    std::array<double, MAX_ASIA_HISTORY> asia_range_history_{};
    size_t asia_range_idx_ = 0;
    size_t asia_range_count_ = 0;
    
    void pushATR(double atr) {
        atr_history_[atr_history_idx_] = atr;
        atr_history_idx_ = (atr_history_idx_ + 1) % MAX_ATR_HISTORY;
        if (atr_history_count_ < MAX_ATR_HISTORY) atr_history_count_++;
    }
    
    void pushOR(double or_range) {
        or_history_[or_history_idx_] = or_range;
        or_history_idx_ = (or_history_idx_ + 1) % MAX_OR_HISTORY;
        if (or_history_count_ < MAX_OR_HISTORY) or_history_count_++;
    }
    
    void pushAsiaRange(double range) {
        asia_range_history_[asia_range_idx_] = range;
        asia_range_idx_ = (asia_range_idx_ + 1) % MAX_ASIA_HISTORY;
        if (asia_range_count_ < MAX_ASIA_HISTORY) asia_range_count_++;
    }
    
    [[nodiscard]] double medianATR() const {
        if (atr_history_count_ == 0) return 0.0;
        
        std::array<double, MAX_ATR_HISTORY> sorted;
        std::copy(atr_history_.begin(), atr_history_.begin() + atr_history_count_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + atr_history_count_);
        
        return sorted[atr_history_count_ / 2];
    }
    
    [[nodiscard]] double computeORPercentile(double or_range) const {
        if (or_history_count_ == 0) return 0.5;
        
        size_t count_below = 0;
        for (size_t i = 0; i < or_history_count_; i++) {
            if (or_history_[i] < or_range) count_below++;
        }
        
        return static_cast<double>(count_below) / or_history_count_;
    }
    
    [[nodiscard]] double computeAsiaRangePercentile(double range) const {
        if (asia_range_count_ == 0) return 0.5;
        
        size_t count_below = 0;
        for (size_t i = 0; i < asia_range_count_; i++) {
            if (asia_range_history_[i] < range) count_below++;
        }
        
        return static_cast<double>(count_below) / asia_range_count_;
    }
    
    [[nodiscard]] static double computeStdDev(const double* data, size_t n) {
        if (n < 2) return 0.0;
        
        double sum = 0.0;
        for (size_t i = 0; i < n; i++) sum += data[i];
        double mean = sum / n;
        
        double sq_sum = 0.0;
        for (size_t i = 0; i < n; i++) {
            double diff = data[i] - mean;
            sq_sum += diff * diff;
        }
        
        return std::sqrt(sq_sum / (n - 1));
    }
};

// =============================================================================
// GLOBAL MARKET QUALITY CUTS ACCESSOR
// =============================================================================
inline MarketQualityCuts& getMarketQualityCuts() {
    static MarketQualityCuts instance;
    return instance;
}

} // namespace Chimera
