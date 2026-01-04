// ═══════════════════════════════════════════════════════════════════════════════
// cfd_engine/include/config/CapitalScaling.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// VERSION: v1.0.0
// OWNER: Jo
//
// PRINCIPLE:
// Scale only when conditions are best; never via leverage creep.
// Never increase leverage to "recover." Scaling is conditional, not emotional.
//
// BASE RISK:
// - Per trade: 0.25% – 0.50% equity
// - Max concurrent CFDs: 2
//
// SCALE-UP (SAFE - NON-MARTINGALE):
// - Allowed only if open PnL ≥ +0.5R
// - One add max, +50% of initial size
// - Stop → break-even on total position
// - Forbidden in TRANSITION regime
// - Session multiplier must be ≥1.2×
//
// DRAWDOWN GUARDS:
// - Daily max DD: 2.0R → trading stops
// - Weekly max DD: 3.5R → size halves next week
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace chimera {
namespace cfd {
namespace config {

// ═══════════════════════════════════════════════════════════════════════════════
// REGIME ENUM (if not already defined elsewhere)
// ═══════════════════════════════════════════════════════════════════════════════

enum class MarketRegime : uint8_t {
    UNKNOWN = 0,
    TRENDING,
    RANGING,
    TRANSITION,
    VOLATILE
};

inline const char* regime_str(MarketRegime r) noexcept {
    switch (r) {
        case MarketRegime::UNKNOWN:    return "UNKNOWN";
        case MarketRegime::TRENDING:   return "TRENDING";
        case MarketRegime::RANGING:    return "RANGING";
        case MarketRegime::TRANSITION: return "TRANSITION";
        case MarketRegime::VOLATILE:   return "VOLATILE";
        default: return "???";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CAPITAL CONFIG
// ═══════════════════════════════════════════════════════════════════════════════

struct CapitalConfig {
    // ─────────────────────────────────────────────────────────────────────────
    // Base Risk Settings
    // ─────────────────────────────────────────────────────────────────────────
    double base_risk_pct = 0.25;        // 0.25% per trade (conservative)
    double max_risk_pct = 0.50;         // 0.50% maximum
    int max_concurrent_positions = 2;   // Max 2 CFD positions at once
    
    // ─────────────────────────────────────────────────────────────────────────
    // Drawdown Guards
    // ─────────────────────────────────────────────────────────────────────────
    double daily_max_dd_r = 2.0;        // 2.0R daily → trading stops
    double weekly_max_dd_r = 3.5;       // 3.5R weekly → size halves next week
    double session_max_dd_r = 1.0;      // 1.0R per session → pause session
    
    // ─────────────────────────────────────────────────────────────────────────
    // Scale-Up Configuration (SAFE)
    // ─────────────────────────────────────────────────────────────────────────
    bool scale_up_enabled = true;
    double scale_up_min_open_r = 0.5;   // Must be +0.5R before adding
    int scale_up_max_adds = 1;          // Maximum 1 add
    double scale_up_add_fraction = 0.5; // Add 50% of initial size
    double scale_up_min_session_mult = 1.2; // Session multiplier >= 1.2
    bool scale_up_move_stop_be = true;  // Move stop to break-even after add
    
    // ─────────────────────────────────────────────────────────────────────────
    // Recovery Rules
    // ─────────────────────────────────────────────────────────────────────────
    double size_after_weekly_dd = 0.5;  // 50% size after hitting weekly DD
    int recovery_days = 5;              // Days at reduced size before normal
    
    // Factory for default config
    static CapitalConfig defaults() {
        return CapitalConfig();
    }
    
    // Factory for conservative config
    static CapitalConfig conservative() {
        CapitalConfig cfg;
        cfg.base_risk_pct = 0.15;
        cfg.max_risk_pct = 0.25;
        cfg.max_concurrent_positions = 1;
        cfg.daily_max_dd_r = 1.5;
        cfg.scale_up_enabled = false;
        return cfg;
    }
    
    // Factory for aggressive config (NOT RECOMMENDED)
    static CapitalConfig aggressive() {
        CapitalConfig cfg;
        cfg.base_risk_pct = 0.50;
        cfg.max_risk_pct = 1.0;
        cfg.max_concurrent_positions = 3;
        cfg.daily_max_dd_r = 3.0;
        return cfg;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION MULTIPLIERS
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Session multipliers apply to SIZE, not to spread thresholds
 * These are based on historical edge analysis per session
 */
struct SessionMultipliers {
    double asia = 0.6;              // Low liquidity, wider spreads
    double pre_london = 0.8;        // Building liquidity
    double london_open = 1.4;       // High volatility, good opportunities
    double london = 1.0;            // Normal
    double london_ny_overlap = 1.2; // Best liquidity
    double ny_open = 1.6;           // Highest volatility
    double ny_mid = 1.0;            // Normal
    double ny_close = 0.8;          // Reducing liquidity
    double post_ny = 0.7;           // Low liquidity
    
    static SessionMultipliers defaults() {
        return SessionMultipliers();
    }
};

/**
 * Get session multiplier by session enum
 * (Matches TradingSession from BlackBullSpreadTables.hpp)
 */
inline double get_session_multiplier(int session_id, const SessionMultipliers& mults = SessionMultipliers()) noexcept {
    switch (session_id) {
        case 1: return mults.asia;
        case 2: return mults.pre_london;
        case 3: return mults.london_open;
        case 4: return mults.london;
        case 5: return mults.london_ny_overlap;
        case 6: return mults.ny_open;
        case 7: return mults.ny_mid;
        case 8: return mults.ny_close;
        case 9: return mults.post_ny;
        default: return 0.0; // OFF
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SCALE-UP GATE
// ═══════════════════════════════════════════════════════════════════════════════

struct ScaleUpGateResult {
    bool allowed;
    const char* block_reason;
    double add_size;            // Size to add (0 if blocked)
    double new_stop_price;      // Break-even stop after add
    
    explicit operator bool() const noexcept { return allowed; }
};

/**
 * Check if scale-up is allowed
 * 
 * Requirements (ALL must be true):
 * 1. Position already +0.5R
 * 2. Spread still ≤ threshold (caller must verify)
 * 3. Edge direction unchanged (caller must verify)
 * 4. Displacement expanding (not stalling) (caller must verify)
 * 5. Session multiplier ≥ 1.2×
 * 6. Not in TRANSITION regime
 * 7. Haven't exceeded max adds
 */
inline ScaleUpGateResult check_scale_up(
    const CapitalConfig& config,
    double current_pnl_r,           // Current unrealized PnL in R units
    double session_multiplier,      // Current session multiplier
    MarketRegime regime,            // Current market regime
    int adds_so_far,                // How many adds already made
    double initial_size,            // Initial position size
    double entry_price,             // Original entry price
    double current_price,           // Current price
    bool is_long                    // Position direction
) noexcept {
    ScaleUpGateResult result;
    result.allowed = false;
    result.add_size = 0.0;
    result.new_stop_price = 0.0;
    
    // Check if scale-up is enabled
    if (!config.scale_up_enabled) {
        result.block_reason = "SCALE_UP_DISABLED";
        return result;
    }
    
    // Check PnL requirement
    if (current_pnl_r < config.scale_up_min_open_r) {
        result.block_reason = "PNL_TOO_LOW";
        return result;
    }
    
    // Check max adds
    if (adds_so_far >= config.scale_up_max_adds) {
        result.block_reason = "MAX_ADDS_REACHED";
        return result;
    }
    
    // Check session multiplier
    if (session_multiplier < config.scale_up_min_session_mult) {
        result.block_reason = "SESSION_MULT_LOW";
        return result;
    }
    
    // Check regime - TRANSITION is forbidden
    if (regime == MarketRegime::TRANSITION) {
        result.block_reason = "TRANSITION_REGIME";
        return result;
    }
    
    // All checks passed
    result.allowed = true;
    result.block_reason = nullptr;
    result.add_size = initial_size * config.scale_up_add_fraction;
    
    // Calculate break-even stop
    // BE stop = weighted average entry of all positions
    if (config.scale_up_move_stop_be) {
        // After adding, total size = initial + add
        double total_size = initial_size + result.add_size;
        // New average entry
        double avg_entry = (entry_price * initial_size + current_price * result.add_size) / total_size;
        // BE stop = avg entry (plus/minus small buffer for spread)
        double buffer = (current_price - entry_price) * 0.05; // 5% of move as buffer
        result.new_stop_price = is_long ? (avg_entry - buffer) : (avg_entry + buffer);
    }
    
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// DRAWDOWN MANAGER
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * DrawdownState - Tracks drawdown across different timeframes
 */
struct DrawdownState {
    // Session tracking
    double session_start_equity = 0.0;
    double session_pnl_r = 0.0;
    double session_max_dd_r = 0.0;
    
    // Daily tracking
    double daily_start_equity = 0.0;
    double daily_pnl_r = 0.0;
    double daily_max_dd_r = 0.0;
    double daily_high_r = 0.0;
    
    // Weekly tracking
    double weekly_start_equity = 0.0;
    double weekly_pnl_r = 0.0;
    double weekly_max_dd_r = 0.0;
    double weekly_high_r = 0.0;
    
    // State flags
    bool session_stopped = false;
    bool daily_stopped = false;
    bool weekly_size_reduced = false;
    int recovery_days_remaining = 0;
    
    void reset_session(double equity) noexcept {
        session_start_equity = equity;
        session_pnl_r = 0.0;
        session_max_dd_r = 0.0;
        session_stopped = false;
    }
    
    void reset_daily(double equity) noexcept {
        daily_start_equity = equity;
        daily_pnl_r = 0.0;
        daily_max_dd_r = 0.0;
        daily_high_r = 0.0;
        daily_stopped = false;
        reset_session(equity);
    }
    
    void reset_weekly(double equity) noexcept {
        weekly_start_equity = equity;
        weekly_pnl_r = 0.0;
        weekly_max_dd_r = 0.0;
        weekly_high_r = 0.0;
        weekly_size_reduced = false;
        reset_daily(equity);
    }
};

/**
 * Update drawdown state after a trade
 */
inline void update_drawdown(
    DrawdownState& state,
    double trade_pnl_r,
    const CapitalConfig& config
) noexcept {
    // Update session
    state.session_pnl_r += trade_pnl_r;
    if (state.session_pnl_r < -state.session_max_dd_r) {
        state.session_max_dd_r = -state.session_pnl_r;
    }
    if (state.session_max_dd_r >= config.session_max_dd_r) {
        state.session_stopped = true;
    }
    
    // Update daily
    state.daily_pnl_r += trade_pnl_r;
    if (state.daily_pnl_r > state.daily_high_r) {
        state.daily_high_r = state.daily_pnl_r;
    }
    double daily_dd = state.daily_high_r - state.daily_pnl_r;
    if (daily_dd > state.daily_max_dd_r) {
        state.daily_max_dd_r = daily_dd;
    }
    if (state.daily_max_dd_r >= config.daily_max_dd_r) {
        state.daily_stopped = true;
    }
    
    // Update weekly
    state.weekly_pnl_r += trade_pnl_r;
    if (state.weekly_pnl_r > state.weekly_high_r) {
        state.weekly_high_r = state.weekly_pnl_r;
    }
    double weekly_dd = state.weekly_high_r - state.weekly_pnl_r;
    if (weekly_dd > state.weekly_max_dd_r) {
        state.weekly_max_dd_r = weekly_dd;
    }
    if (state.weekly_max_dd_r >= config.weekly_max_dd_r) {
        state.weekly_size_reduced = true;
        state.recovery_days_remaining = config.recovery_days;
    }
}

/**
 * Get current size multiplier based on drawdown state
 */
inline double get_drawdown_size_multiplier(
    const DrawdownState& state,
    const CapitalConfig& config
) noexcept {
    if (state.daily_stopped) return 0.0;
    if (state.session_stopped) return 0.0;
    if (state.weekly_size_reduced) return config.size_after_weekly_dd;
    return 1.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// POSITION SIZER
// ═══════════════════════════════════════════════════════════════════════════════

struct PositionSizeResult {
    double size;                    // Final position size (in lots/units)
    double risk_pct;                // Actual risk percentage used
    double risk_amount;             // Risk in account currency
    double session_mult;            // Session multiplier applied
    double dd_mult;                 // Drawdown multiplier applied
    double spread_mult;             // Spread-based multiplier applied
    bool allowed;                   // Is trading allowed?
    const char* block_reason;       // If blocked, why
};

/**
 * Calculate position size with all multipliers applied
 * 
 * @param equity Account equity
 * @param stop_distance_pct Stop distance as % of entry
 * @param session_multiplier Current session multiplier
 * @param spread_mult Spread-based size multiplier (from spread gate)
 * @param dd_state Current drawdown state
 * @param config Capital configuration
 * @param concurrent_positions Current number of open positions
 */
inline PositionSizeResult calculate_position_size(
    double equity,
    double stop_distance_pct,
    double session_multiplier,
    double spread_mult,
    const DrawdownState& dd_state,
    const CapitalConfig& config,
    int concurrent_positions = 0
) noexcept {
    PositionSizeResult result;
    result.size = 0.0;
    result.risk_pct = 0.0;
    result.risk_amount = 0.0;
    result.session_mult = session_multiplier;
    result.spread_mult = spread_mult;
    result.allowed = true;
    result.block_reason = nullptr;
    
    // Check if trading is allowed
    if (dd_state.daily_stopped) {
        result.allowed = false;
        result.block_reason = "DAILY_DD_LIMIT";
        return result;
    }
    
    if (dd_state.session_stopped) {
        result.allowed = false;
        result.block_reason = "SESSION_DD_LIMIT";
        return result;
    }
    
    // Check concurrent positions
    if (concurrent_positions >= config.max_concurrent_positions) {
        result.allowed = false;
        result.block_reason = "MAX_POSITIONS";
        return result;
    }
    
    // Get drawdown multiplier
    result.dd_mult = get_drawdown_size_multiplier(dd_state, config);
    if (result.dd_mult <= 0.0) {
        result.allowed = false;
        result.block_reason = "DD_BLOCKED";
        return result;
    }
    
    // Calculate base risk
    result.risk_pct = config.base_risk_pct;
    
    // Apply all multipliers to risk
    double adjusted_risk_pct = result.risk_pct 
                               * session_multiplier 
                               * spread_mult 
                               * result.dd_mult;
    
    // Cap at max risk
    adjusted_risk_pct = std::min(adjusted_risk_pct, config.max_risk_pct);
    
    // Calculate risk amount
    result.risk_amount = equity * (adjusted_risk_pct / 100.0);
    
    // Calculate position size
    // Size = Risk Amount / (Entry Price * Stop Distance %)
    // This gives us size where: loss at stop = risk amount
    if (stop_distance_pct > 0) {
        result.size = result.risk_amount / stop_distance_pct;
    }
    
    result.risk_pct = adjusted_risk_pct;
    
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CAPITAL RULES SUMMARY (FOR DOCUMENTATION)
// ═══════════════════════════════════════════════════════════════════════════════

/*
 * ┌─────────────────────────────────────────────────────────────────────────────┐
 * │                        CAPITAL & RISK RULES                                  │
 * ├─────────────────────────────────────────────────────────────────────────────┤
 * │ BASE RISK                                                                    │
 * │   • Per trade: 0.25% – 0.50% equity                                         │
 * │   • Max concurrent CFDs: 2                                                  │
 * ├─────────────────────────────────────────────────────────────────────────────┤
 * │ SESSION MULTIPLIERS (apply to SIZE, not thresholds)                         │
 * │   Asia:        0.6×     │  London Open: 1.4×   │  NY Open: 1.6×            │
 * │   Pre-London:  0.8×     │  London→NY:   1.2×   │  NY Mid:  1.0×            │
 * │   Post-NY:     0.7×     │  NY Close:    0.8×   │                           │
 * ├─────────────────────────────────────────────────────────────────────────────┤
 * │ SCALE-UP (SAFE)                                                             │
 * │   ✓ Only if open PnL ≥ +0.5R                                               │
 * │   ✓ One add max, +50% of initial size                                      │
 * │   ✓ Stop → break-even on total position                                    │
 * │   ✗ Forbidden in TRANSITION regime                                         │
 * │   ✗ Session multiplier must be ≥1.2×                                       │
 * ├─────────────────────────────────────────────────────────────────────────────┤
 * │ DRAWDOWN GUARDS                                                             │
 * │   • Session: 1.0R → pause session                                          │
 * │   • Daily:   2.0R → trading stops                                          │
 * │   • Weekly:  3.5R → size halves next week                                  │
 * ├─────────────────────────────────────────────────────────────────────────────┤
 * │ HARD RULES                                                                   │
 * │   ✗ Never add to losers                                                    │
 * │   ✗ Never add outside priority sessions                                    │
 * │   ✗ Never add in chop/transition                                           │
 * │   ✗ Never increase leverage to "recover"                                   │
 * │   ✗ Scaling is conditional, not emotional                                  │
 * └─────────────────────────────────────────────────────────────────────────────┘
 */

} // namespace config
} // namespace cfd
} // namespace chimera
