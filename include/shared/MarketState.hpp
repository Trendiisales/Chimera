// =============================================================================
// MarketState.hpp - Centralized Market State Classification
// =============================================================================
// PURPOSE: Compute market state ONCE per tick, shared by all strategies.
// This replaces scattered indicator checks with explicit state-driven logic.
//
// ARCHITECTURE:
//   - CentralMicroEngine computes raw signals (VWAP, OFI, VPIN, etc.)
//   - MarketStateClassifier classifies those signals into discrete states
//   - Strategies check state + intent, not raw indicators
//
// STATES (simplified from 5-state for HFT):
//   - DEAD: No edge, skip (low vol, wide spread, toxic flow)
//   - TRENDING: Momentum plays, continuation setups
//   - RANGING: Mean reversion, fade extremes
//   - VOLATILE: Reduced size, wider stops, fast exit
//
// TRADE INTENT:
//   - NO_TRADE: Risk conditions not met
//   - MOMENTUM: Trend following, breakout continuation
//   - MEAN_REVERSION: Fade overextension, counter-trend
// =============================================================================
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <atomic>

namespace Chimera {

// =============================================================================
// Market State Enumeration
// =============================================================================
enum class MarketState : uint8_t {
    DEAD       = 0,  // No edge - skip trading
    TRENDING   = 1,  // Directional moves - momentum strategies
    RANGING    = 2,  // Mean reverting - fade strategies  
    VOLATILE   = 3   // High vol - reduce size, widen stops
};

// =============================================================================
// Trade Intent - What TYPE of trade is allowed in this state
// =============================================================================
enum class TradeIntent : uint8_t {
    NO_TRADE       = 0,  // Skip - conditions not met
    MOMENTUM       = 1,  // Trend following, breakouts
    MEAN_REVERSION = 2   // Fade, counter-trend
};

// =============================================================================
// Conviction Level - How aggressively to size
// =============================================================================
enum class ConvictionLevel : uint8_t {
    SKIP   = 0,  // Don't trade (score < 4)
    LOW    = 1,  // 0.25x normal size (score 4-5)
    NORMAL = 2,  // 1.0x normal size (score 6-7)
    HIGH   = 3,  // 1.5x normal size (score 8+)
    A_PLUS = 4   // 2.0x normal size (score 9+, rare)
};

// =============================================================================
// MarketStateSnapshot - Immutable snapshot for strategies to read
// =============================================================================
struct alignas(64) MarketStateSnapshot {
    // Classification
    MarketState state = MarketState::DEAD;
    TradeIntent intent = TradeIntent::NO_TRADE;
    ConvictionLevel conviction = ConvictionLevel::SKIP;
    
    // Conviction score (0-10)
    int conviction_score = 0;
    
    // Raw regime factors (for transparency/debugging)
    double vol_z = 1.0;       // current_vol / baseline_vol
    double spread_z = 1.0;    // current_spread / median_spread
    double ofi_z = 0.0;       // order flow imbalance [-1, +1]
    double vpin_level = 0.0;  // toxicity [0, 1]
    double trend_strength = 0.0;  // momentum consistency [0, 1]
    
    // Thresholds (for logging)
    double vpin_threshold = 0.60;
    double spread_threshold = 10.0;
    
    // State reason (for GUI display)
    char reason[32] = {0};
    
    // Timestamp
    uint64_t ts_ns = 0;
    
    // Helpers
    bool canTrade() const { return intent != TradeIntent::NO_TRADE; }
    bool isMomentum() const { return intent == TradeIntent::MOMENTUM; }
    bool isReversion() const { return intent == TradeIntent::MEAN_REVERSION; }
    
    double sizeMultiplier() const {
        switch (conviction) {
            case ConvictionLevel::A_PLUS: return 2.0;
            case ConvictionLevel::HIGH:   return 1.5;
            case ConvictionLevel::NORMAL: return 1.0;
            case ConvictionLevel::LOW:    return 0.5;
            default: return 0.0;
        }
    }
};

// =============================================================================
// MarketStateClassifier - Computes state from microstructure signals
// =============================================================================
class MarketStateClassifier {
public:
    // Configuration thresholds
    struct Config {
        // DEAD state thresholds - v6.72 RELAXED for live trading
        double vpin_max = 0.75;           // Was 0.60 - now tolerates more flow imbalance
        double spread_max_bps = 25.0;     // Was 10.0 - wider spreads OK for indices/metals
        double vol_min = 0.05;            // Was 0.15 - lower vol threshold
        
        // VOLATILE state thresholds
        double vol_high = 4.0;            // Was 3.0 - more tolerance
        double spread_high_z = 3.5;       // Was 2.5 - more tolerance
        
        // TRENDING vs RANGING - v6.72 RELAXED
        double trend_threshold = 0.25;    // Was 0.4 - easier to detect trends
        double ofi_trend_confirm = 0.15;  // Was 0.3 - easier OFI confirmation
        
        // Conviction scoring weights
        double weight_vpin = 2.0;         // Low VPIN = +points
        double weight_spread = 1.5;       // Tight spread = +points
        double weight_trend = 2.0;        // Strong trend = +points
        double weight_flow = 1.5;         // Aligned flow = +points
        double weight_vol = 1.0;          // Good volatility = +points
    };
    
    MarketStateClassifier() = default;
    
    // ==========================================================================
    // Main classification function
    // Call this ONCE per tick with microstructure data
    // ==========================================================================
    MarketStateSnapshot classify(
        double ofi,              // Order flow imbalance [-1, +1]
        double vpin,             // Volume-synchronized PIN [0, 1]
        double spread_bps,       // Current spread in basis points
        double realized_vol,     // Realized volatility
        double trend_strength,   // Momentum consistency [0, 1]
        double momentum,         // Price momentum (direction)
        double median_spread_bps,// Baseline spread for normalization
        double baseline_vol,     // Baseline vol for normalization
        uint64_t ts_ns           // Timestamp
    ) {
        MarketStateSnapshot snap;
        snap.ts_ns = ts_ns;
        snap.ofi_z = ofi;
        snap.vpin_level = vpin;
        snap.trend_strength = trend_strength;
        snap.vpin_threshold = cfg_.vpin_max;
        snap.spread_threshold = cfg_.spread_max_bps;
        
        // Compute z-scores
        snap.vol_z = (baseline_vol > 0) ? (realized_vol / baseline_vol) : 1.0;
        snap.spread_z = (median_spread_bps > 0) ? (spread_bps / median_spread_bps) : 1.0;
        
        // =====================================================================
        // STEP 1: Check for DEAD state (no trading)
        // =====================================================================
        if (vpin > cfg_.vpin_max) {
            snap.state = MarketState::DEAD;
            snap.intent = TradeIntent::NO_TRADE;
            snap.conviction = ConvictionLevel::SKIP;
            snprintf(snap.reason, sizeof(snap.reason), "TOXIC_FLOW:%.2f", vpin);
            return snap;
        }
        
        if (spread_bps > cfg_.spread_max_bps) {
            snap.state = MarketState::DEAD;
            snap.intent = TradeIntent::NO_TRADE;
            snap.conviction = ConvictionLevel::SKIP;
            snprintf(snap.reason, sizeof(snap.reason), "WIDE_SPREAD:%.1f", spread_bps);
            return snap;
        }
        
        if (realized_vol < cfg_.vol_min && baseline_vol > 0) {
            snap.state = MarketState::DEAD;
            snap.intent = TradeIntent::NO_TRADE;
            snap.conviction = ConvictionLevel::SKIP;
            snprintf(snap.reason, sizeof(snap.reason), "LOW_VOL:%.4f", realized_vol);
            return snap;
        }
        
        // =====================================================================
        // STEP 2: Check for VOLATILE state
        // =====================================================================
        if (snap.vol_z > cfg_.vol_high || snap.spread_z > cfg_.spread_high_z) {
            snap.state = MarketState::VOLATILE;
            // In VOLATILE, prefer mean reversion (fade extremes)
            snap.intent = TradeIntent::MEAN_REVERSION;
            snprintf(snap.reason, sizeof(snap.reason), "HIGH_VOL:%.2f", snap.vol_z);
            // Reduce conviction in volatile conditions
            snap.conviction_score = computeConviction(snap, momentum);
            snap.conviction_score = std::max(0, snap.conviction_score - 2);
            snap.conviction = scoreToLevel(snap.conviction_score);
            return snap;
        }
        
        // =====================================================================
        // STEP 3: TRENDING vs RANGING
        // =====================================================================
        bool has_trend = trend_strength > cfg_.trend_threshold;
        bool flow_aligned = (momentum > 0 && ofi > cfg_.ofi_trend_confirm) ||
                           (momentum < 0 && ofi < -cfg_.ofi_trend_confirm);
        
        if (has_trend && flow_aligned) {
            snap.state = MarketState::TRENDING;
            snap.intent = TradeIntent::MOMENTUM;
            snprintf(snap.reason, sizeof(snap.reason), "TREND:%.2f", trend_strength);
        } else {
            snap.state = MarketState::RANGING;
            snap.intent = TradeIntent::MEAN_REVERSION;
            snprintf(snap.reason, sizeof(snap.reason), "RANGE:%.2f", trend_strength);
        }
        
        // =====================================================================
        // STEP 4: Compute conviction score
        // =====================================================================
        snap.conviction_score = computeConviction(snap, momentum);
        snap.conviction = scoreToLevel(snap.conviction_score);
        
        // No trade if conviction too low
        if (snap.conviction == ConvictionLevel::SKIP) {
            snap.intent = TradeIntent::NO_TRADE;
        }
        
        return snap;
    }
    
    // Configuration access
    Config& config() { return cfg_; }
    const Config& config() const { return cfg_; }
    
private:
    Config cfg_;
    
    // Compute conviction score (0-10)
    int computeConviction(const MarketStateSnapshot& snap, double momentum) {
        int score = 0;
        
        // Low VPIN is good (+0 to +2 points)
        if (snap.vpin_level < 0.3) score += 2;
        else if (snap.vpin_level < 0.5) score += 1;
        
        // Tight spread is good (+0 to +2 points)
        if (snap.spread_z < 0.7) score += 2;
        else if (snap.spread_z < 1.0) score += 1;
        
        // Strong trend/reversion is good (+0 to +2 points)
        if (snap.trend_strength > 0.7) score += 2;
        else if (snap.trend_strength > 0.5) score += 1;
        
        // Aligned flow is good (+0 to +2 points)
        double flow_alignment = (momentum > 0) ? snap.ofi_z : -snap.ofi_z;
        if (flow_alignment > 0.5) score += 2;
        else if (flow_alignment > 0.2) score += 1;
        
        // Good volatility (+0 to +2 points)
        // Not too low (dead), not too high (dangerous)
        if (snap.vol_z > 0.5 && snap.vol_z < 2.0) score += 2;
        else if (snap.vol_z > 0.3 && snap.vol_z < 2.5) score += 1;
        
        return std::min(10, score);
    }
    
    ConvictionLevel scoreToLevel(int score) {
        if (score >= 9) return ConvictionLevel::A_PLUS;
        if (score >= 7) return ConvictionLevel::HIGH;    // Was 8
        if (score >= 5) return ConvictionLevel::NORMAL;  // Was 6
        if (score >= 2) return ConvictionLevel::LOW;     // Was 4 - NOW ALLOWS MORE TRADES
        return ConvictionLevel::SKIP;
    }
};

// =============================================================================
// Helper: State to string (for logging/GUI)
// =============================================================================
inline const char* marketStateStr(MarketState s) {
    switch (s) {
        case MarketState::DEAD:      return "DEAD";
        case MarketState::TRENDING:  return "TRENDING";
        case MarketState::RANGING:   return "RANGING";
        case MarketState::VOLATILE:  return "VOLATILE";
        default: return "UNKNOWN";
    }
}

inline const char* tradeIntentStr(TradeIntent i) {
    switch (i) {
        case TradeIntent::NO_TRADE:       return "NO_TRADE";
        case TradeIntent::MOMENTUM:       return "MOMENTUM";
        case TradeIntent::MEAN_REVERSION: return "MEAN_REVERSION";
        default: return "UNKNOWN";
    }
}

inline const char* convictionStr(ConvictionLevel c) {
    switch (c) {
        case ConvictionLevel::SKIP:   return "SKIP";
        case ConvictionLevel::LOW:    return "LOW";
        case ConvictionLevel::NORMAL: return "NORMAL";
        case ConvictionLevel::HIGH:   return "HIGH";
        case ConvictionLevel::A_PLUS: return "A+";
        default: return "UNKNOWN";
    }
}

} // namespace Chimera
