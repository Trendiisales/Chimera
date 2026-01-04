// ═══════════════════════════════════════════════════════════════════════════════
// include/alpha/AlphaSelector.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: INSTITUTIONAL ALPHA MODULE SELECTION
//
// PURPOSE: Select the appropriate alpha module based on market regime.
// Alpha modules are MUTUALLY EXCLUSIVE - only one runs at a time.
//
// ALPHA TYPES:
// - TREND_PULLBACK:  Trend continuation on pullbacks
// - RANGE_FADE:      Mean reversion at range extremes
// - VOL_BREAKOUT:    Momentum bursts on volatility expansion
// - NONE:            No trade (DEAD regime or invalid conditions)
//
// CRITICAL: If AlphaType is NONE, the system MUST NOT trade.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "MarketRegime.hpp"
#include <cstdint>

namespace Chimera {
namespace Alpha {

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Type Enumeration
// ─────────────────────────────────────────────────────────────────────────────
enum class AlphaType : uint8_t {
    NONE            = 0,  // No alpha - DO NOT TRADE
    TREND_PULLBACK  = 1,  // Trend: Buy pullbacks in uptrend, sell rallies in downtrend
    RANGE_FADE      = 2,  // Range: Fade extremes, target mean
    VOL_BREAKOUT    = 3   // Volatility: Ride expansion, momentum first
};

inline const char* alphaTypeStr(AlphaType a) {
    switch (a) {
        case AlphaType::NONE:           return "NONE";
        case AlphaType::TREND_PULLBACK: return "TREND_PULLBACK";
        case AlphaType::RANGE_FADE:     return "RANGE_FADE";
        case AlphaType::VOL_BREAKOUT:   return "VOL_BREAKOUT";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Status (for registry)
// ─────────────────────────────────────────────────────────────────────────────
enum class AlphaStatus : uint8_t {
    ACTIVE   = 0,  // Alpha is live, can trade
    RETIRED  = 1,  // Alpha failed metrics, disabled
    COOLDOWN = 2,  // Alpha in cooldown period after retirement
    SHADOW   = 3   // Alpha in shadow mode, paper trading only
};

inline const char* alphaStatusStr(AlphaStatus s) {
    switch (s) {
        case AlphaStatus::ACTIVE:   return "ACTIVE";
        case AlphaStatus::RETIRED:  return "RETIRED";
        case AlphaStatus::COOLDOWN: return "COOLDOWN";
        case AlphaStatus::SHADOW:   return "SHADOW";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Selection Result
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaSelection {
    AlphaType type = AlphaType::NONE;
    AlphaStatus status = AlphaStatus::ACTIVE;
    double confidence = 0.0;
    const char* reason = "NONE";
    bool allow_trade = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Select Alpha Based on Regime (Hard Gate)
// ─────────────────────────────────────────────────────────────────────────────
inline AlphaType selectAlpha(MarketRegime r) {
    switch (r) {
        case MarketRegime::TREND:      return AlphaType::TREND_PULLBACK;
        case MarketRegime::RANGE:      return AlphaType::RANGE_FADE;
        case MarketRegime::VOLATILITY: return AlphaType::VOL_BREAKOUT;
        case MarketRegime::DEAD:
        default:                       return AlphaType::NONE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Validity Check - Does the snapshot support this alpha?
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaValidityConfig {
    // Trend alpha requirements
    double trend_min_strength = 0.55;
    double trend_max_pullback = 0.40;    // Max pullback depth (Fib 0.382+)
    
    // Range alpha requirements
    double range_min_score = 0.60;
    
    // Volatility alpha requirements  
    double vol_min_momentum = 0.50;
};

inline bool isAlphaValid(AlphaType alpha, const MarketSnapshot& s,
                         const AlphaValidityConfig& cfg = AlphaValidityConfig{}) {
    switch (alpha) {
        case AlphaType::TREND_PULLBACK:
            // Trend must be strong, pullback not too deep
            return s.trend_strength > cfg.trend_min_strength &&
                   s.pullback_depth < cfg.trend_max_pullback;
            
        case AlphaType::RANGE_FADE:
            // Must be in range and at extreme
            return s.range_score > cfg.range_min_score &&
                   s.at_range_extreme;
            
        case AlphaType::VOL_BREAKOUT:
            // Must have volatility expansion
            return s.volatility_expansion &&
                   (s.volume_spike || s.range_compression_prior);
            
        case AlphaType::NONE:
        default:
            return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Full Alpha Selection with Validation
// ─────────────────────────────────────────────────────────────────────────────
inline AlphaSelection selectAndValidateAlpha(
    MarketRegime regime,
    const MarketSnapshot& snapshot,
    const AlphaValidityConfig& cfg = AlphaValidityConfig{}
) {
    AlphaSelection result;
    
    // Get alpha for regime
    result.type = selectAlpha(regime);
    
    if (result.type == AlphaType::NONE) {
        result.reason = "REGIME_DEAD";
        result.confidence = 0.9;
        result.allow_trade = false;
        return result;
    }
    
    // Check if alpha is valid for current snapshot
    if (!isAlphaValid(result.type, snapshot, cfg)) {
        result.reason = "ALPHA_NOT_VALID";
        result.confidence = 0.7;
        result.allow_trade = false;
        return result;
    }
    
    // Alpha is valid
    result.reason = "ALPHA_VALID";
    result.confidence = 0.8;
    result.allow_trade = true;
    result.status = AlphaStatus::ACTIVE;
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Health Metrics (for auto-retirement)
// ─────────────────────────────────────────────────────────────────────────────
struct AlphaHealth {
    AlphaType type = AlphaType::NONE;
    
    // Trade statistics
    int trades = 0;
    int wins = 0;
    double expectancy = 0.0;         // Average R per trade
    double drawdown = 0.0;           // Max drawdown in R
    double sharpe_like = 0.0;        // Win rate adjusted returns
    int consecutive_losses = 0;
    
    // Computed metrics
    double win_rate() const {
        return trades > 0 ? static_cast<double>(wins) / trades : 0.0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Retirement Rules (Hard, Deterministic)
// ─────────────────────────────────────────────────────────────────────────────
struct RetirementConfig {
    int min_trades_for_decision = 50;
    double min_expectancy = -0.10;        // Below this = retire
    int max_consecutive_losses = 12;
    double max_drawdown_percent = 3.0;
};

inline bool shouldRetire(const AlphaHealth& h, 
                         const RetirementConfig& cfg = RetirementConfig{}) {
    // Need enough trades to decide
    if (h.trades < cfg.min_trades_for_decision) {
        return false;
    }
    
    // Negative expectancy
    if (h.expectancy < cfg.min_expectancy) {
        return true;
    }
    
    // Too many consecutive losses
    if (h.consecutive_losses >= cfg.max_consecutive_losses) {
        return true;
    }
    
    // Excessive drawdown
    if (h.drawdown > cfg.max_drawdown_percent) {
        return true;
    }
    
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Registry - Tracks status of each alpha
// ─────────────────────────────────────────────────────────────────────────────
class AlphaRegistry {
public:
    static constexpr size_t NUM_ALPHAS = 4;  // NONE, TREND, RANGE, VOL
    
    AlphaRegistry() {
        for (size_t i = 0; i < NUM_ALPHAS; i++) {
            status_[i] = AlphaStatus::ACTIVE;
            retired_at_[i] = 0;
            health_[i] = AlphaHealth{};
            health_[i].type = static_cast<AlphaType>(i);
        }
    }
    
    // Check if alpha is tradeable
    bool isActive(AlphaType a) const {
        return status_[static_cast<size_t>(a)] == AlphaStatus::ACTIVE;
    }
    
    AlphaStatus getStatus(AlphaType a) const {
        return status_[static_cast<size_t>(a)];
    }
    
    // Retire an alpha
    void retire(AlphaType a, uint64_t now_ns) {
        size_t idx = static_cast<size_t>(a);
        status_[idx] = AlphaStatus::RETIRED;
        retired_at_[idx] = now_ns;
    }
    
    // Put alpha in cooldown (for potential re-evaluation)
    void cooldown(AlphaType a, uint64_t now_ns) {
        size_t idx = static_cast<size_t>(a);
        status_[idx] = AlphaStatus::COOLDOWN;
        retired_at_[idx] = now_ns;
    }
    
    // Reactivate an alpha
    void activate(AlphaType a) {
        size_t idx = static_cast<size_t>(a);
        status_[idx] = AlphaStatus::ACTIVE;
        retired_at_[idx] = 0;
        // Reset health for fresh evaluation
        health_[idx] = AlphaHealth{};
        health_[idx].type = a;
    }
    
    // Update health metrics
    void recordTrade(AlphaType a, bool win, double pnl_R) {
        size_t idx = static_cast<size_t>(a);
        auto& h = health_[idx];
        
        h.trades++;
        if (win) {
            h.wins++;
            h.consecutive_losses = 0;
        } else {
            h.consecutive_losses++;
        }
        
        // Update expectancy (rolling average)
        double alpha = 0.1;  // Decay factor
        h.expectancy = (1.0 - alpha) * h.expectancy + alpha * pnl_R;
        
        // Update drawdown
        if (pnl_R < 0) {
            h.drawdown = std::max(h.drawdown, std::abs(pnl_R));
        }
    }
    
    // Check for auto-retirement
    void evaluateRetirement(uint64_t now_ns) {
        for (size_t i = 1; i < NUM_ALPHAS; i++) {  // Skip NONE
            if (status_[i] == AlphaStatus::ACTIVE) {
                if (shouldRetire(health_[i])) {
                    retire(static_cast<AlphaType>(i), now_ns);
                }
            }
        }
    }
    
    // Check for cooldown recovery (after 7 days)
    static constexpr uint64_t COOLDOWN_NS = 7ULL * 24 * 3600 * 1'000'000'000ULL;
    
    void evaluateCooldown(uint64_t now_ns) {
        for (size_t i = 1; i < NUM_ALPHAS; i++) {
            if (status_[i] == AlphaStatus::COOLDOWN) {
                if (now_ns - retired_at_[i] > COOLDOWN_NS) {
                    // Move to shadow for re-evaluation
                    status_[i] = AlphaStatus::SHADOW;
                }
            }
        }
    }
    
    const AlphaHealth& getHealth(AlphaType a) const {
        return health_[static_cast<size_t>(a)];
    }
    
    void reset() {
        for (size_t i = 0; i < NUM_ALPHAS; i++) {
            status_[i] = AlphaStatus::ACTIVE;
            retired_at_[i] = 0;
            health_[i] = AlphaHealth{};
            health_[i].type = static_cast<AlphaType>(i);
        }
    }
    
private:
    AlphaStatus status_[NUM_ALPHAS];
    uint64_t retired_at_[NUM_ALPHAS] = {};
    AlphaHealth health_[NUM_ALPHAS];
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Alpha Registry
// ─────────────────────────────────────────────────────────────────────────────
inline AlphaRegistry& getAlphaRegistry() {
    static AlphaRegistry registry;
    return registry;
}

} // namespace Alpha
} // namespace Chimera
