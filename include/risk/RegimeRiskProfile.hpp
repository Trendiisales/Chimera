// ═══════════════════════════════════════════════════════════════════════════════
// include/risk/RegimeRiskProfile.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: REGIME-SPECIFIC RISK PROFILES
//
// PURPOSE: Risk adapts with regime. This is huge for survival.
//
// PROFILES BY REGIME:
// - TREND:      Higher size, allow scaling, ride winners
// - RANGE:      Lower size, no scaling, tight stops
// - VOLATILITY: Medium size, no scaling, quick exits
// - DEAD:       Zero size, no trading
//
// CRITICAL: Nothing trades without matching regime + risk profile.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "../alpha/MarketRegime.hpp"
#include <cstdint>
#include <algorithm>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Risk Profile - Per-regime risk settings
// ─────────────────────────────────────────────────────────────────────────────
struct RiskProfile {
    // Size control
    double max_risk_per_trade = 0.50;  // % of equity per trade
    double max_daily_risk = 2.0;       // % of equity per day
    double size_multiplier = 1.0;      // Applied to base size
    
    // Scaling control
    bool allow_scaling = false;        // Allow adding to winners
    int max_scale_levels = 0;          // Max pyramid levels
    double scale_threshold_R = 0.0;    // Min R before scaling
    
    // Stop/target control
    double stop_mult = 1.0;            // Multiplier on base stop
    double target_mult = 1.0;          // Multiplier on base target
    bool use_trailing = false;         // Use trailing stop
    
    // Time control
    int max_hold_minutes = 60;         // Max hold time
    bool force_eod_exit = true;        // Exit before end of day
};

// ─────────────────────────────────────────────────────────────────────────────
// Get Risk Profile for Regime
// ─────────────────────────────────────────────────────────────────────────────
inline RiskProfile riskForRegime(Alpha::MarketRegime r) {
    RiskProfile p;
    
    switch (r) {
        case Alpha::MarketRegime::TREND:
            // Trend: Ride winners, allow scaling
            p.max_risk_per_trade = 0.50;
            p.max_daily_risk = 2.0;
            p.size_multiplier = 1.2;
            p.allow_scaling = true;
            p.max_scale_levels = 2;
            p.scale_threshold_R = 0.5;
            p.stop_mult = 1.2;           // Wider stops
            p.target_mult = 1.5;         // Bigger targets
            p.use_trailing = true;
            p.max_hold_minutes = 120;
            p.force_eod_exit = true;
            break;
            
        case Alpha::MarketRegime::RANGE:
            // Range: Fade extremes, tight control
            p.max_risk_per_trade = 0.25;
            p.max_daily_risk = 1.0;
            p.size_multiplier = 0.8;
            p.allow_scaling = false;
            p.max_scale_levels = 0;
            p.stop_mult = 0.8;           // Tighter stops
            p.target_mult = 0.8;         // Smaller targets
            p.use_trailing = false;      // Fixed targets
            p.max_hold_minutes = 30;
            p.force_eod_exit = true;
            break;
            
        case Alpha::MarketRegime::VOLATILITY:
            // Volatility: Hit and run
            p.max_risk_per_trade = 0.40;
            p.max_daily_risk = 1.5;
            p.size_multiplier = 1.0;
            p.allow_scaling = false;
            p.max_scale_levels = 0;
            p.stop_mult = 1.0;
            p.target_mult = 1.2;         // Bigger targets on expansion
            p.use_trailing = true;       // Lock in gains
            p.max_hold_minutes = 15;     // Quick exits
            p.force_eod_exit = true;
            break;
            
        case Alpha::MarketRegime::DEAD:
        default:
            // Dead: NO TRADING
            p.max_risk_per_trade = 0.0;
            p.max_daily_risk = 0.0;
            p.size_multiplier = 0.0;
            p.allow_scaling = false;
            p.max_hold_minutes = 0;
            break;
    }
    
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Risk Adjustment Result
// ─────────────────────────────────────────────────────────────────────────────
struct RiskAdjustment {
    double size_mult = 1.0;           // Final size multiplier
    double stop_mult = 1.0;           // Stop distance multiplier
    double target_mult = 1.0;         // Target distance multiplier
    bool allow_trade = true;          // Can we trade at all?
    bool allow_scale = false;         // Can we add to position?
    const char* reason = "NORMAL";
};

// ─────────────────────────────────────────────────────────────────────────────
// Calculate Risk Adjustment for Current Conditions
// ─────────────────────────────────────────────────────────────────────────────
struct RiskContext {
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
    double current_drawdown_pct = 0.0;
    double daily_pnl_pct = 0.0;
    int consecutive_losses = 0;
    int consecutive_wins = 0;
    double governor_heat = 0.0;
    int positions_open = 0;
    int max_positions = 3;
};

inline RiskAdjustment calculateRiskAdjustment(const RiskContext& ctx) {
    RiskAdjustment adj;
    
    // Get base profile for regime
    RiskProfile profile = riskForRegime(ctx.regime);
    
    // Start with regime profile
    adj.size_mult = profile.size_multiplier;
    adj.stop_mult = profile.stop_mult;
    adj.target_mult = profile.target_mult;
    adj.allow_scale = profile.allow_scaling;
    
    // Dead regime = no trade
    if (ctx.regime == Alpha::MarketRegime::DEAD) {
        adj.allow_trade = false;
        adj.size_mult = 0.0;
        adj.reason = "REGIME_DEAD";
        return adj;
    }
    
    // Drawdown adjustment
    if (ctx.current_drawdown_pct > 3.0) {
        // Severe drawdown: reduce significantly
        adj.size_mult *= 0.4;
        adj.allow_scale = false;
        adj.reason = "DD_SEVERE";
    } else if (ctx.current_drawdown_pct > 2.0) {
        // Moderate drawdown
        adj.size_mult *= 0.6;
        adj.allow_scale = false;
        adj.reason = "DD_MODERATE";
    } else if (ctx.current_drawdown_pct > 1.0) {
        // Light drawdown
        adj.size_mult *= 0.8;
        adj.reason = "DD_LIGHT";
    }
    
    // Daily P&L adjustment
    if (ctx.daily_pnl_pct < -1.5) {
        // Down big today: reduce
        adj.size_mult *= 0.5;
        adj.allow_scale = false;
        adj.reason = "DAILY_DOWN";
    } else if (ctx.daily_pnl_pct > 2.0) {
        // Up big today: can be slightly more aggressive
        adj.size_mult *= 1.1;
    }
    
    // Consecutive losses adjustment
    if (ctx.consecutive_losses >= 5) {
        adj.size_mult *= 0.5;
        adj.allow_scale = false;
        adj.reason = "LOSS_STREAK";
    } else if (ctx.consecutive_losses >= 3) {
        adj.size_mult *= 0.75;
    }
    
    // Consecutive wins: slight boost but careful
    if (ctx.consecutive_wins >= 5) {
        // Too many wins = getting cocky, reduce slightly
        adj.size_mult *= 0.9;
    } else if (ctx.consecutive_wins >= 3) {
        // Modest winning streak
        adj.size_mult *= 1.05;
    }
    
    // Governor heat adjustment
    if (ctx.governor_heat > 0.7) {
        adj.size_mult *= (1.0 - ctx.governor_heat * 0.5);
        adj.reason = "GOV_HEAT";
    }
    
    // Position count adjustment
    if (ctx.positions_open >= ctx.max_positions - 1) {
        adj.size_mult *= 0.7;  // Reduce size when near limit
        adj.allow_scale = false;
    }
    
    // Clamp final multiplier
    adj.size_mult = std::clamp(adj.size_mult, 0.2, 1.5);
    
    // Check if effectively zero
    if (adj.size_mult < 0.25) {
        adj.allow_trade = false;
        adj.reason = "SIZE_TOO_SMALL";
    }
    
    return adj;
}

// ─────────────────────────────────────────────────────────────────────────────
// Position Size Calculator (with regime awareness)
// ─────────────────────────────────────────────────────────────────────────────
struct PositionSizeResult {
    double size = 0.0;                // Final position size (units)
    double risk_per_trade = 0.0;      // Actual risk in currency
    double stop_distance = 0.0;       // Stop distance in price
    bool approved = false;
    const char* reason = "UNKNOWN";
};

inline PositionSizeResult calculatePositionSize(
    double equity,
    double entry_price,
    double base_stop_bps,
    double base_size,
    const RiskContext& ctx
) {
    PositionSizeResult result;
    
    // Get adjustment
    RiskAdjustment adj = calculateRiskAdjustment(ctx);
    
    if (!adj.allow_trade) {
        result.approved = false;
        result.reason = adj.reason;
        return result;
    }
    
    // Get profile for limits
    RiskProfile profile = riskForRegime(ctx.regime);
    
    // Calculate stop distance
    double stop_bps = base_stop_bps * adj.stop_mult;
    result.stop_distance = entry_price * stop_bps / 10000.0;
    
    // Calculate risk per trade
    double max_risk = equity * profile.max_risk_per_trade / 100.0;
    result.risk_per_trade = std::min(max_risk, equity * 0.01);  // Hard cap 1%
    
    // Calculate size based on risk
    double risk_based_size = result.risk_per_trade / result.stop_distance;
    
    // Apply adjustments to base size
    double adjusted_base = base_size * adj.size_mult;
    
    // Use smaller of risk-based and adjusted base
    result.size = std::min(risk_based_size, adjusted_base);
    
    // Sanity clamps
    result.size = std::max(0.0, result.size);
    
    result.approved = result.size > 0;
    result.reason = adj.reason;
    
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Risk Profile Manager (for symbol-specific overrides)
// ─────────────────────────────────────────────────────────────────────────────
class RiskProfileManager {
public:
    static constexpr size_t MAX_OVERRIDES = 16;
    
    // Set symbol-specific override
    void setOverride(const char* symbol, Alpha::MarketRegime regime, 
                     const RiskProfile& profile) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i], symbol) == 0 && regimes_[i] == regime) {
                profiles_[i] = profile;
                return;
            }
        }
        if (count_ < MAX_OVERRIDES) {
            strncpy(symbols_[count_], symbol, 15);
            regimes_[count_] = regime;
            profiles_[count_] = profile;
            count_++;
        }
    }
    
    // Get profile (with override check)
    RiskProfile getProfile(const char* symbol, Alpha::MarketRegime regime) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i], symbol) == 0 && regimes_[i] == regime) {
                return profiles_[i];
            }
        }
        return riskForRegime(regime);
    }
    
private:
    char symbols_[MAX_OVERRIDES][16] = {};
    Alpha::MarketRegime regimes_[MAX_OVERRIDES] = {};
    RiskProfile profiles_[MAX_OVERRIDES];
    size_t count_ = 0;
};

inline RiskProfileManager& getRiskProfileManager() {
    static RiskProfileManager mgr;
    return mgr;
}

} // namespace Risk
} // namespace Chimera
