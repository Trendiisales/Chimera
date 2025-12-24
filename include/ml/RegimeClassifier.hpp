// =============================================================================
// RegimeClassifier.hpp - Volatility Regime Classification for Model Routing
// =============================================================================
// PURPOSE: Classify current volatility regime for ML model routing
// DESIGN:
//   - LOW_VOL: Low volatility, mean reversion favorable
//   - NORMAL_VOL: Typical conditions
//   - HIGH_VOL: Elevated volatility, reduce size
//   - CRISIS: Extreme conditions, defensive mode
//
// USAGE:
//   RegimeClassifier classifier;
//   classifier.update(atr_percentile, index_drawdown);
//   MLRegime regime = classifier.getRegime();
//   
//   // Use regime to select model
//   MLDecision decision = engine.inferWithRegime(features, regime);
//
// FEATURES USED:
//   - ATR percentile (rolling rank of current ATR)
//   - Index drawdown (SPY/NDX distance from recent high)
//   - VIX level (if available)
//   - Spread expansion
// =============================================================================
#pragma once

#include "MLTypes.hpp"
#include <algorithm>
#include <cmath>
#include <array>
#include <deque>

namespace Chimera {
namespace ML {

// =============================================================================
// Regime Classifier Configuration
// =============================================================================
struct RegimeConfig {
    // ATR percentile thresholds
    double low_vol_atr_pct = 0.30;     // Below 30th percentile = LOW_VOL
    double high_vol_atr_pct = 0.80;    // Above 80th percentile = HIGH_VOL
    
    // Drawdown thresholds (market index)
    double crisis_drawdown = 0.08;     // -8% from peak = CRISIS
    
    // VIX thresholds (if using)
    double vix_low = 15.0;
    double vix_high = 25.0;
    double vix_crisis = 35.0;
    
    // Spread expansion factor
    double spread_high = 2.0;          // 2x normal = HIGH_VOL signal
    
    // Regime stickiness (hysteresis)
    int min_ticks_in_regime = 100;     // Min ticks before allowing change
    
    // EMA periods for calculations
    int atr_fast_period = 20;
    int atr_slow_period = 100;
};

// =============================================================================
// Rolling Percentile Calculator
// =============================================================================
class RollingPercentile {
public:
    explicit RollingPercentile(size_t window = 500) noexcept 
        : window_(window) {}
    
    void update(double value) noexcept {
        values_.push_back(value);
        if (values_.size() > window_) {
            values_.pop_front();
        }
    }
    
    double percentile(double value) const noexcept {
        if (values_.empty()) return 0.5;
        
        size_t count_below = 0;
        for (double v : values_) {
            if (v < value) count_below++;
        }
        return static_cast<double>(count_below) / values_.size();
    }
    
    double currentPercentile() const noexcept {
        if (values_.empty()) return 0.5;
        return percentile(values_.back());
    }
    
    void reset() noexcept { values_.clear(); }
    size_t size() const noexcept { return values_.size(); }
    
private:
    std::deque<double> values_;
    size_t window_;
};

// =============================================================================
// Regime Classifier
// =============================================================================
class RegimeClassifier {
public:
    RegimeClassifier() noexcept 
        : RegimeClassifier(RegimeConfig()) {}
    
    explicit RegimeClassifier(const RegimeConfig& config) noexcept
        : config_(config)
        , current_regime_(MLRegime::NORMAL_VOL)
        , ticks_in_regime_(0)
        , atr_percentile_(500)
    {}
    
    // =========================================================================
    // Core Classification
    // =========================================================================
    
    // Classify based on ATR percentile and index drawdown
    MLRegime classify(double atr_percentile, double index_drawdown) noexcept {
        MLRegime candidate;
        
        // CRISIS takes priority
        if (index_drawdown < -config_.crisis_drawdown) {
            candidate = MLRegime::CRISIS;
        }
        // HIGH_VOL next
        else if (atr_percentile > config_.high_vol_atr_pct) {
            candidate = MLRegime::HIGH_VOL;
        }
        // LOW_VOL
        else if (atr_percentile < config_.low_vol_atr_pct) {
            candidate = MLRegime::LOW_VOL;
        }
        // NORMAL
        else {
            candidate = MLRegime::NORMAL_VOL;
        }
        
        // Apply stickiness
        if (candidate == current_regime_) {
            ticks_in_regime_++;
        } else if (ticks_in_regime_ >= config_.min_ticks_in_regime) {
            current_regime_ = candidate;
            ticks_in_regime_ = 0;
        } else {
            ticks_in_regime_++;
        }
        
        return current_regime_;
    }
    
    // Classify with VIX input
    MLRegime classifyWithVIX(double atr_percentile, double index_drawdown, 
                             double vix) noexcept {
        // VIX overrides for extreme cases
        if (vix > config_.vix_crisis) {
            updateRegime(MLRegime::CRISIS);
            return current_regime_;
        }
        if (vix > config_.vix_high) {
            updateRegime(MLRegime::HIGH_VOL);
            return current_regime_;
        }
        if (vix < config_.vix_low) {
            // Low VIX doesn't force LOW_VOL, but adds confidence
            MLRegime candidate = classify(atr_percentile, index_drawdown);
            if (candidate == MLRegime::NORMAL_VOL) {
                updateRegime(MLRegime::LOW_VOL);
            }
            return current_regime_;
        }
        
        return classify(atr_percentile, index_drawdown);
    }
    
    // Update with raw ATR value (auto-computes percentile)
    void updateATR(double atr) noexcept {
        atr_percentile_.update(atr);
    }
    
    // Get current ATR percentile
    double getATRPercentile() const noexcept {
        return atr_percentile_.currentPercentile();
    }
    
    // =========================================================================
    // Accessors
    // =========================================================================
    
    MLRegime getRegime() const noexcept { return current_regime_; }
    int ticksInRegime() const noexcept { return ticks_in_regime_; }
    
    void setRegime(MLRegime r) noexcept {
        current_regime_ = r;
        ticks_in_regime_ = 0;
    }
    
    void reset() noexcept {
        current_regime_ = MLRegime::NORMAL_VOL;
        ticks_in_regime_ = 0;
        atr_percentile_.reset();
    }
    
    RegimeConfig& config() noexcept { return config_; }
    const RegimeConfig& config() const noexcept { return config_; }
    
private:
    void updateRegime(MLRegime r) noexcept {
        if (r != current_regime_) {
            current_regime_ = r;
            ticks_in_regime_ = 0;
        } else {
            ticks_in_regime_++;
        }
    }
    
private:
    RegimeConfig config_;
    MLRegime current_regime_;
    int ticks_in_regime_;
    RollingPercentile atr_percentile_;
};

// =============================================================================
// Quick Regime Classification (standalone function)
// =============================================================================
inline MLRegime quickClassifyRegime(double atr_percentile, double index_drawdown) noexcept {
    if (index_drawdown < -0.08) return MLRegime::CRISIS;
    if (atr_percentile > 0.80) return MLRegime::HIGH_VOL;
    if (atr_percentile < 0.30) return MLRegime::LOW_VOL;
    return MLRegime::NORMAL_VOL;
}

} // namespace ML
} // namespace Chimera
