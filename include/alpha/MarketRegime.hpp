// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/MarketRegime.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: INSTITUTIONAL MARKET REGIME DETECTION
//
// PURPOSE: Classify the market into one of four mutually exclusive regimes
// for alpha module selection. Institutions SWITCH alphas, they don't blend them.
//
// REGIMES:
// - TREND:      Strong directional movement, breakout/pullback strategies work
// - RANGE:      Mean-reverting, fade extremes strategies work
// - VOLATILITY: Expansion/compression, momentum burst strategies work
// - DEAD:       No structure, NO TRADE (this is valid - forcing trades here = loss)
//
// CRITICAL PRINCIPLE: Only ONE alpha is active at a time per regime.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Market Regime Enumeration
// ─────────────────────────────────────────────────────────────────────────────
enum class MarketRegime : uint8_t {
    DEAD       = 0,  // No structure - NO TRADE
    TREND      = 1,  // Directional continuation
    RANGE      = 2,  // Mean reverting
    VOLATILITY = 3   // Expansion/bursts
};

inline const char* regimeStr(MarketRegime r) {
    switch (r) {
        case MarketRegime::DEAD:       return "DEAD";
        case MarketRegime::TREND:      return "TREND";
        case MarketRegime::RANGE:      return "RANGE";
        case MarketRegime::VOLATILITY: return "VOLATILITY";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Market Snapshot - Input data for regime detection
// ─────────────────────────────────────────────────────────────────────────────
struct MarketSnapshot {
    // Trend metrics (0-1 scale)
    double trend_strength = 0.0;       // ADX/directional strength
    double momentum_strength = 0.0;    // RSI/momentum normalized
    double pullback_depth = 0.0;       // Current pullback as % of move
    
    // Range metrics (0-1 scale)
    double range_score = 0.0;          // Bollinger/range compression
    bool at_range_extreme = false;     // Near band edge
    bool exhaustion_signal = false;    // Volume/momentum exhaustion
    
    // Volatility metrics
    bool volatility_expansion = false; // ATR expanding
    bool range_compression_prior = false; // Squeeze before expansion
    bool volume_spike = false;         // Significant volume increase
    
    // Structure quality
    double structure_clarity = 0.0;    // 0 = chaos, 1 = clear structure
    
    // Price action
    double atr_percentile = 0.5;       // ATR rank in rolling window
    double spread_expansion = 1.0;     // Current spread vs normal (1.0 = normal)
};

// ─────────────────────────────────────────────────────────────────────────────
// Regime Detection Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct RegimeConfig {
    // Trend thresholds
    double trend_strength_min = 0.55;     // Min for TREND regime
    double momentum_strength_min = 0.50;
    
    // Range thresholds
    double range_score_min = 0.60;        // Min for RANGE regime
    
    // Volatility thresholds
    double atr_percentile_high = 0.75;    // Above this = potential VOLATILITY
    
    // Dead zone thresholds
    double structure_clarity_min = 0.35;  // Below this = DEAD
    double spread_expansion_max = 2.5;    // Above this = DEAD (liquidity crisis)
    
    // Regime stickiness (prevents flapping)
    // v4.9.12 HARDENING: Doubled from 50 to 100 for institutional stability
    int min_ticks_in_regime = 100;
    
    // Additional confirmation after initial detection
    int regime_confirm_ticks = 25;
    
    // Priority weights for tie-breaking
    double volatility_priority = 1.3;     // Volatility wins over trend/range
};

// ─────────────────────────────────────────────────────────────────────────────
// Regime Detection Result
// ─────────────────────────────────────────────────────────────────────────────
struct RegimeResult {
    MarketRegime regime = MarketRegime::DEAD;
    double confidence = 0.0;           // 0-1 confidence in classification
    const char* reason = "UNKNOWN";    // Human-readable reason
    int ticks_in_regime = 0;           // How long we've been in this regime
};

// ─────────────────────────────────────────────────────────────────────────────
// Regime Detector - Stateful, with hysteresis
// ─────────────────────────────────────────────────────────────────────────────
class RegimeDetector {
public:
    RegimeDetector() : RegimeDetector(RegimeConfig{}) {}
    explicit RegimeDetector(const RegimeConfig& cfg) : config_(cfg) {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Core Classification
    // ─────────────────────────────────────────────────────────────────────────
    RegimeResult detect(const MarketSnapshot& s) {
        RegimeResult result;
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 0: DEAD if no structure or liquidity crisis
        // ─────────────────────────────────────────────────────────────────────
        if (s.structure_clarity < config_.structure_clarity_min ||
            s.spread_expansion > config_.spread_expansion_max) {
            result.regime = MarketRegime::DEAD;
            result.reason = s.spread_expansion > config_.spread_expansion_max 
                          ? "SPREAD_CRISIS" : "NO_STRUCTURE";
            result.confidence = 0.85;
            return applyStickiness(result);
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 1: VOLATILITY takes priority (burst/expansion)
        // ─────────────────────────────────────────────────────────────────────
        if (s.volatility_expansion && 
            (s.range_compression_prior || s.volume_spike)) {
            double vol_score = 0.0;
            if (s.volatility_expansion) vol_score += 0.4;
            if (s.range_compression_prior) vol_score += 0.3;
            if (s.volume_spike) vol_score += 0.3;
            
            if (vol_score >= 0.5) {
                result.regime = MarketRegime::VOLATILITY;
                result.reason = "VOL_EXPANSION";
                result.confidence = std::min(0.95, vol_score * config_.volatility_priority);
                return applyStickiness(result);
            }
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 2: TREND if directional and momentum aligned
        // ─────────────────────────────────────────────────────────────────────
        if (s.trend_strength > config_.trend_strength_min &&
            s.momentum_strength > config_.momentum_strength_min) {
            result.regime = MarketRegime::TREND;
            result.reason = "DIRECTIONAL";
            result.confidence = std::min(0.90, (s.trend_strength + s.momentum_strength) / 2.0);
            return applyStickiness(result);
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Rule 3: RANGE if consolidating
        // ─────────────────────────────────────────────────────────────────────
        if (s.range_score > config_.range_score_min) {
            result.regime = MarketRegime::RANGE;
            result.reason = "CONSOLIDATING";
            result.confidence = std::min(0.85, s.range_score);
            return applyStickiness(result);
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Default: DEAD (ambiguous state - don't force)
        // ─────────────────────────────────────────────────────────────────────
        result.regime = MarketRegime::DEAD;
        result.reason = "AMBIGUOUS";
        result.confidence = 0.5;
        return applyStickiness(result);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Quick Classification (stateless, no hysteresis)
    // ─────────────────────────────────────────────────────────────────────────
    static MarketRegime quickClassify(const MarketSnapshot& s) {
        // Volatility first
        if (s.volatility_expansion && s.momentum_strength > 0.6) {
            return MarketRegime::VOLATILITY;
        }
        
        // Trend
        if (s.trend_strength > 0.6) {
            return MarketRegime::TREND;
        }
        
        // Range
        if (s.range_score > 0.65) {
            return MarketRegime::RANGE;
        }
        
        return MarketRegime::DEAD;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────
    MarketRegime currentRegime() const { return current_regime_; }
    int ticksInRegime() const { return ticks_in_regime_; }
    double confidence() const { return last_confidence_; }
    const char* lastReason() const { return last_reason_; }
    
    void reset() {
        current_regime_ = MarketRegime::DEAD;
        ticks_in_regime_ = 0;
        last_confidence_ = 0.0;
        last_reason_ = "RESET";
    }
    
    RegimeConfig& config() { return config_; }
    const RegimeConfig& config() const { return config_; }
    
private:
    RegimeResult applyStickiness(RegimeResult result) {
        if (result.regime == current_regime_) {
            ticks_in_regime_++;
        } else {
            // Only switch if we've been stable long enough
            if (ticks_in_regime_ >= config_.min_ticks_in_regime ||
                result.confidence > last_confidence_ + 0.15) {
                current_regime_ = result.regime;
                ticks_in_regime_ = 0;
            } else {
                // Keep previous regime, increment counter
                result.regime = current_regime_;
                ticks_in_regime_++;
            }
        }
        
        result.ticks_in_regime = ticks_in_regime_;
        last_confidence_ = result.confidence;
        last_reason_ = result.reason;
        return result;
    }
    
private:
    RegimeConfig config_;
    MarketRegime current_regime_ = MarketRegime::DEAD;
    int ticks_in_regime_ = 0;
    double last_confidence_ = 0.0;
    const char* last_reason_ = "INIT";
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Regime Detector Instance
// ─────────────────────────────────────────────────────────────────────────────
inline RegimeDetector& getRegimeDetector() {
    static RegimeDetector detector;
    return detector;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Regime Detectors
// ─────────────────────────────────────────────────────────────────────────────
class SymbolRegimeManager {
public:
    static constexpr size_t MAX_SYMBOLS = 16;
    
    RegimeDetector* get(const char* symbol) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i], symbol) == 0) {
                return &detectors_[i];
            }
        }
        if (count_ < MAX_SYMBOLS) {
            strncpy(symbols_[count_], symbol, 15);
            return &detectors_[count_++];
        }
        return nullptr;  // Table full
    }
    
    void resetAll() {
        for (size_t i = 0; i < count_; i++) {
            detectors_[i].reset();
        }
    }
    
private:
    char symbols_[MAX_SYMBOLS][16] = {};
    RegimeDetector detectors_[MAX_SYMBOLS];
    size_t count_ = 0;
};

inline SymbolRegimeManager& getSymbolRegimeManager() {
    static SymbolRegimeManager mgr;
    return mgr;
}

} // namespace Alpha
} // namespace Chimera
