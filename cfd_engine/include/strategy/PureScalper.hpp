// =============================================================================
// PureScalper.hpp v4.2.2 - Complete Institutional HFT System
// =============================================================================
// v4.2.2: COMPLETE SYSTEM - Session-aware, PnL-weighted, self-healing
//
// CORE FEATURES:
//   - Per-Symbol Microstructure Profiles: Deterministic burst/confirm params
//   - Expectancy Memory: Per-symbol EMA of PnL-normalized expectancy
//   - Expectancy-Weighted Confirmation: Positive exp → faster, Negative → slower
//   - Session State Machine: OPEN (burst-first), ACTIVE (full), FADE (no entry)
//   - Capital Allocator: Score-based budget distribution across symbols
//   - Kill-Switch Ladder: Latency/slippage/drawdown safety system
//   - Auto-Blacklist: Disable symbols with net_pnl <= -3 × avg_win
//   - Diagnostic Counters: bursts/confirms/trades per symbol
//
// GOLDEN RULES:
//   1. Gate on RAW edge, size on SCALED edge
//   2. Positive expectancy rewards, negative protects
//   3. Session state controls risk multiplier
//   4. Capital flows to what's working
//   5. Kill-switch overrides everything
//
// KEY INVARIANT (NON-NEGOTIABLE):
//   If edge < cost × safety → trade must not exist.
//   NO forex shortcut. NO metals exception. NO indices override.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <cstring>
#include <vector>
#include "MicroStateMachine.hpp"
#include "../../../include/risk/SymbolHealth.hpp"
#include "../../../include/microstructure/EdgeController.hpp"
#include "../../../include/metrics/TradeOpportunityMetrics.hpp"
#include "../../../include/micro/MicrostructureProfiles.hpp"
#include "../../../include/risk/KillSwitchLadder.hpp"
#include "../../../include/risk/KillSwitchAnalytics.hpp"
#include "../../../include/risk/CapitalAllocator.hpp"
#include "../../../include/execution/VenueRoutingPolicy.hpp"
#include "../../../include/execution/CrossVenueArbGuard.hpp"
#include "../../../include/metrics/PrometheusMetrics.hpp"

namespace Omega {

// =============================================================================
// HFT PROFILE - Per-asset-class execution parameters
// Same gate, different numbers. No exceptions.
// =============================================================================
struct HFTProfile {
    // Edge gating (THE INVARIANT)
    double max_spread_bps;        // Maximum spread to trade
    double min_edge_bps;          // Absolute minimum edge floor
    double min_edge_mult;         // Edge must be >= cost × this
    double slippage_bps;          // Expected slippage
    
    // Microstructure
    double chop_band_bps;         // Min displacement for entry
    double vol_cap_mult;          // Edge capped at vol × this
    
    // Risk / exits
    double min_sl_floor_bps;      // Absolute SL floor
    double tp_bps;                // Take profit target
    double sl_bps;                // Stop loss
    
    // Frequency control
    uint64_t cooldown_after_loss_ns;      // Cooldown after loss
    uint64_t min_time_between_trades_ns;  // Min time between trades
    uint64_t max_hold_ns;                 // Max hold time
};

// =============================================================================
// ASSET CLASS PROFILES - Production-grade, broker-aware
// =============================================================================

// FX MAJORS: Tight spread, strong mean reversion, false micro momentum
inline constexpr HFTProfile FX_MAJOR_PROFILE {
    .max_spread_bps              = 1.5,
    .min_edge_bps                = 4.0,
    .min_edge_mult               = 2.8,
    .slippage_bps                = 0.5,
    .chop_band_bps               = 2.0,
    .vol_cap_mult                = 0.8,
    .min_sl_floor_bps            = 4.0,
    .tp_bps                      = 10.0,
    .sl_bps                      = 4.0,
    .cooldown_after_loss_ns      = 400'000'000,   // 400ms
    .min_time_between_trades_ns  = 3'000'000'000, // 3s
    .max_hold_ns                 = 8'000'000'000  // 8s
};

// =============================================================================
// SESSION STATE MACHINE - Explicit regime control (CRITICAL FOR HFT)
// =============================================================================
enum class SessionState {
    OPEN,    // Auction / shock - burst-first mode (first 10 mins)
    ACTIVE,  // Normal trading - trend+signal required
    FADE     // Post-open decay / lunch - no new entries
};

inline uint32_t GetSessionSeconds() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    struct tm* utc_tm = gmtime(&now_c);
    if (!utc_tm) return 999999;  // Safe fallback to FADE
    
    int utc_hour = utc_tm->tm_hour;
    int utc_min = utc_tm->tm_min;
    int utc_sec = utc_tm->tm_sec;
    
    // Calculate seconds since NY open (14:30 UTC = 9:30 ET)
    // Or seconds since London open (08:00 UTC)
    int session_mins = utc_hour * 60 + utc_min;
    
    // NY session: 14:30-21:00 UTC
    if (session_mins >= 14*60+30 && session_mins < 21*60) {
        return (session_mins - (14*60+30)) * 60 + utc_sec;
    }
    
    // London session: 08:00-16:30 UTC
    if (session_mins >= 8*60 && session_mins < 16*60+30) {
        return (session_mins - 8*60) * 60 + utc_sec;
    }
    
    return 999999;  // Outside session
}

inline SessionState GetSessionState() {
    uint32_t sec = GetSessionSeconds();
    
    if (sec < 600)        return SessionState::OPEN;    // First 10 mins
    if (sec < 14400)      return SessionState::ACTIVE;  // 4 hours main session
    return SessionState::FADE;
}

inline const char* SessionStateStr(SessionState ss) {
    switch (ss) {
        case SessionState::OPEN:   return "OPEN";
        case SessionState::ACTIVE: return "ACTIVE";
        case SessionState::FADE:   return "FADE";
    }
    return "UNKNOWN";
}

inline double SessionRiskMultiplier(SessionState ss) {
    switch (ss) {
        case SessionState::OPEN:   return 0.7;   // Cautious participation
        case SessionState::ACTIVE: return 1.0;   // Full aggression
        case SessionState::FADE:   return 0.0;   // No new risk
    }
    return 0.0;
}

// Legacy compatibility
inline bool IsSessionOpen(uint64_t /*now_ns*/) {
    return GetSessionState() == SessionState::OPEN;
}

// =============================================================================
// BLACKLISTED SYMBOLS - Structurally incompatible with HFT scalping
// USDJPY: FIX batching + pip value asymmetry + mean-reverting microstructure
// =============================================================================
inline bool IsSymbolBlacklisted(const char* symbol) {
    if (strstr(symbol, "USDJPY") != nullptr) return true;
    // Add more as discovered
    return false;
}

// =============================================================================
// TIGHTENED PROFILE FOR XAUUSD - Higher burst quality requirements
// =============================================================================
inline constexpr HFTProfile XAUUSD_PROFILE {
    .max_spread_bps              = 2.5,
    .min_edge_bps                = 8.0,     // Was 6.0 - require more edge
    .min_edge_mult               = 3.5,     // Was 3.0 - stricter cost check
    .slippage_bps                = 1.0,
    .chop_band_bps               = 4.0,     // Was 3.0 - higher displacement needed
    .vol_cap_mult                = 0.9,
    .min_sl_floor_bps            = 6.0,
    .tp_bps                      = 18.0,
    .sl_bps                      = 6.0,
    .cooldown_after_loss_ns      = 1'000'000'000,   // 1000ms (was 700ms)
    .min_time_between_trades_ns  = 8'000'000'000,   // 8s (was 5s)
    .max_hold_ns                 = 12'000'000'000
};

// METALS: Wide spread, jump risk, trend bursts then stall
inline constexpr HFTProfile METALS_PROFILE {
    .max_spread_bps              = 2.5,
    .min_edge_bps                = 6.0,
    .min_edge_mult               = 3.0,
    .slippage_bps                = 1.0,
    .chop_band_bps               = 3.0,
    .vol_cap_mult                = 0.9,
    .min_sl_floor_bps            = 6.0,
    .tp_bps                      = 18.0,
    .sl_bps                      = 6.0,
    .cooldown_after_loss_ns      = 700'000'000,   // 700ms
    .min_time_between_trades_ns  = 5'000'000'000, // 5s
    .max_hold_ns                 = 12'000'000'000 // 12s
};

// INDICES: Momentum bursts, stop-hunts, volatility clustering
inline constexpr HFTProfile INDICES_PROFILE {
    .max_spread_bps              = 2.0,
    .min_edge_bps                = 5.0,
    .min_edge_mult               = 2.5,
    .slippage_bps                = 0.8,
    .chop_band_bps               = 2.5,
    .vol_cap_mult                = 0.9,
    .min_sl_floor_bps            = 5.0,
    .tp_bps                      = 15.0,
    .sl_bps                      = 5.0,
    .cooldown_after_loss_ns      = 500'000'000,   // 500ms
    .min_time_between_trades_ns  = 3'000'000'000, // 3s
    .max_hold_ns                 = 10'000'000'000 // 10s
};

// =============================================================================
// PROFILE RESOLVER - No ambiguity, no defaults
// =============================================================================

// v4.2: Normalize symbol names (handles XAUUSD., XAUUSDm, NAS100.cash etc)
inline std::string NormalizeSymbol(std::string s) {
    // Remove trailing dots, 'm' suffix, '.cash' etc
    auto end_pos = s.find_last_not_of(".m");
    if (end_pos != std::string::npos) {
        s = s.substr(0, end_pos + 1);
    }
    // Remove .cash suffix
    size_t cash_pos = s.find(".cash");
    if (cash_pos != std::string::npos) {
        s = s.substr(0, cash_pos);
    }
    return s;
}

inline const HFTProfile& ResolveHFTProfile(const std::string& raw_symbol) {
    std::string symbol = NormalizeSymbol(raw_symbol);
    
    // FX MAJORS (excluding USDJPY which is blacklisted)
    if (symbol == "EURUSD" || symbol == "GBPUSD" ||
        symbol == "AUDUSD" || symbol == "USDCAD" || symbol == "AUDNZD" ||
        symbol == "USDCHF" || symbol == "NZDUSD" || symbol == "EURGBP")
        return FX_MAJOR_PROFILE;
    
    // USDJPY - Blacklisted, but return profile if somehow called
    if (symbol == "USDJPY")
        return FX_MAJOR_PROFILE;  // Will be blocked by IsSymbolBlacklisted
    
    // XAUUSD - Special tightened profile
    if (symbol == "XAUUSD")
        return XAUUSD_PROFILE;
    
    // Other METALS
    if (symbol == "XAGUSD")
        return METALS_PROFILE;
    
    // INDICES
    if (symbol == "NAS100" || symbol == "US100" || symbol == "US30" || 
        symbol == "SPX500" || symbol == "US500" || symbol == "GER40" || symbol == "DAX")
        return INDICES_PROFILE;
    
    // UNKNOWN - use most conservative (metals)
    std::cerr << "[HFT-WARN] Unknown symbol " << symbol << " - using METALS profile\n";
    return METALS_PROFILE;
}

// =============================================================================
// LEGACY SYMBOL PROFILE - For backward compatibility with exit logic
// =============================================================================
struct SymbolProfile {
    // Edge gating
    double min_edge_mult = 2.0;        // Edge must be >= spread × this
    double min_conf_entry = 0.80;      // Minimum confidence to enter
    double exit_conf_threshold = 0.80; // Minimum confidence to exit on signal
    
    // Hold discipline
    int64_t min_hold_ms = 2500;        // Minimum time in position
    int64_t max_hold_ms = 30000;       // Maximum time in position
    int64_t time_cooldown_ms = 10000;  // Cooldown between TIME entries
    int64_t sl_cooldown_ms = 15000;    // Cooldown after stop loss
    
    // Flip control
    bool allow_flip = false;           // Allow direction reversal
    bool time_entry_allowed = true;    // Allow TIME-based entries (controlled)
    
    // Risk
    double tp_bps = 45.0;              // Take profit in basis points
    double sl_bps = 25.0;              // Stop loss in basis points
    double trail_start_bps = 20.0;     // Start trailing at this profit
    double trail_stop_bps = 10.0;      // Trail distance
    double max_spread_bps = 12.0;      // Maximum spread to trade
    
    // Session gating (UTC hours, 0-23)
    bool session_gated = false;        // Enable session time gate
    int session_start_hour = 0;        // Session start (UTC)
    int session_start_min = 0;
    int session_end_hour = 24;         // Session end (UTC)
    int session_end_min = 0;
};

// =============================================================================
// GET SYMBOL PROFILE - Updated to use HFT profiles
// =============================================================================
inline SymbolProfile getSymbolProfile(const std::string& symbol) {
    SymbolProfile p;
    const HFTProfile& hft = ResolveHFTProfile(symbol);
    
    // Map HFT profile to legacy profile
    p.min_edge_mult = hft.min_edge_mult;
    p.max_spread_bps = hft.max_spread_bps;
    p.tp_bps = hft.tp_bps;
    p.sl_bps = hft.sl_bps;
    p.min_hold_ms = 1000;  // 1s min hold
    p.max_hold_ms = hft.max_hold_ns / 1'000'000;
    p.sl_cooldown_ms = hft.cooldown_after_loss_ns / 1'000'000;
    p.time_cooldown_ms = hft.min_time_between_trades_ns / 1'000'000;
    
    // Symbol-specific overrides
    if (symbol == "XAUUSD" || symbol == "XAGUSD") {
        p.min_conf_entry = 0.75;
        p.exit_conf_threshold = 0.70;
        p.allow_flip = false;
        p.trail_start_bps = 12.0;
        p.trail_stop_bps = 5.0;
    } else if (symbol == "NAS100" || symbol == "US100" || symbol == "US30" || symbol == "SPX500") {
        p.min_conf_entry = 0.75;
        p.exit_conf_threshold = 0.65;
        p.allow_flip = true;
        p.trail_start_bps = 10.0;
        p.trail_stop_bps = 4.0;
    } else {
        // FX defaults
        p.min_conf_entry = 0.75;
        p.exit_conf_threshold = 0.70;
        p.allow_flip = false;
        p.trail_start_bps = 6.0;
        p.trail_stop_bps = 2.0;
    }
    
    return p;
}

// =============================================================================
// EXPECTANCY TRACKER - Auto-disable losing symbols
// =============================================================================

// v7.11: Symbol trading state
enum class SymbolTradingState : uint8_t {
    LIVE = 0,
    DISABLED_EXPECTANCY = 1,
    PAPER_ONLY = 2
};

struct ExpectancyTracker {
    static constexpr int WINDOW_SIZE = 30;
    static constexpr int MIN_EVAL = 15;
    static constexpr int PAPER_MIN_TRADES = 10;
    
    double pnl_bps[WINDOW_SIZE] = {0};
    double spread_bps[WINDOW_SIZE] = {0};
    int64_t hold_ms[WINDOW_SIZE] = {0};
    int head = 0;
    int count = 0;
    bool disabled = false;
    const char* disable_reason = "";
    
    // v7.11: Paper trading recovery
    SymbolTradingState state = SymbolTradingState::LIVE;
    int paper_trades = 0;
    double paper_net_bps_sum = 0.0;
    
    void recordTrade(double pnl, double spread, int64_t hold) {
        pnl_bps[head] = pnl;
        spread_bps[head] = spread;
        hold_ms[head] = hold;
        head = (head + 1) % WINDOW_SIZE;
        if (count < WINDOW_SIZE) count++;
        
        evaluate();
    }
    
    // v7.11: Record paper trade (simulated)
    void recordPaperTrade(double pnl, double spread) {
        double net = pnl - spread;
        paper_trades++;
        paper_net_bps_sum += net;
        
        // Check for re-enable
        if (paper_trades >= PAPER_MIN_TRADES) {
            double paper_expectancy = paper_net_bps_sum / paper_trades;
            if (paper_expectancy > 0) {
                // Ready to re-enable
                state = SymbolTradingState::LIVE;
                disabled = false;
                disable_reason = "";
                paper_trades = 0;
                paper_net_bps_sum = 0.0;
                // Clear old expectancy data
                count = 0;
                head = 0;
            }
        }
    }
    
    void evaluate() {
        if (count < MIN_EVAL) return;
        
        int n = std::min(count, WINDOW_SIZE);
        double net_sum = 0;
        int wins = 0;
        int flips = 0;  // Trades held < 1s
        double total_hold = 0;
        
        for (int i = 0; i < n; i++) {
            double net = pnl_bps[i] - spread_bps[i];
            net_sum += net;
            if (net > 0) wins++;
            if (hold_ms[i] < 1000) flips++;
            total_hold += hold_ms[i];
        }
        
        double expectancy = net_sum / n;
        double win_rate = (double)wins / n;
        double flip_rate = (double)flips / n;
        double avg_hold = total_hold / n;
        
        // Disable conditions
        if (expectancy < 0) {
            disabled = true;
            disable_reason = "NEG_EXPECTANCY";
            state = SymbolTradingState::PAPER_ONLY;
        } else if (win_rate < 0.35 && count >= 20) {
            disabled = true;
            disable_reason = "LOW_WINRATE";
            state = SymbolTradingState::PAPER_ONLY;
        } else if (flip_rate > 0.10) {
            disabled = true;
            disable_reason = "HIGH_FLIPRATE";
            state = SymbolTradingState::PAPER_ONLY;
        } else if (avg_hold < 1500) {
            disabled = true;
            disable_reason = "AVG_HOLD_LOW";
            state = SymbolTradingState::PAPER_ONLY;
        }
    }
    
    double getExpectancy() const {
        if (count < MIN_EVAL) return 0;
        int n = std::min(count, WINDOW_SIZE);
        double net_sum = 0;
        for (int i = 0; i < n; i++) {
            net_sum += pnl_bps[i] - spread_bps[i];
        }
        return net_sum / n;
    }
    
    // v7.11: Get full stats for GUI
    double getWinRate() const {
        if (count < MIN_EVAL) return 0;
        int n = std::min(count, WINDOW_SIZE);
        int wins = 0;
        for (int i = 0; i < n; i++) {
            if (pnl_bps[i] - spread_bps[i] > 0) wins++;
        }
        return (double)wins / n;
    }
    
    double getFlipRate() const {
        if (count < MIN_EVAL) return 0;
        int n = std::min(count, WINDOW_SIZE);
        int flips = 0;
        for (int i = 0; i < n; i++) {
            if (hold_ms[i] < 1000) flips++;
        }
        return (double)flips / n;
    }
    
    double getAvgHoldMs() const {
        if (count < MIN_EVAL) return 0;
        int n = std::min(count, WINDOW_SIZE);
        double total = 0;
        for (int i = 0; i < n; i++) {
            total += hold_ms[i];
        }
        return total / n;
    }
    
    int getTradeCount() const { return count; }
    bool isPaperMode() const { return state == SymbolTradingState::PAPER_ONLY; }
    
    void reset() {
        count = 0;
        head = 0;
        disabled = false;
        disable_reason = "";
        state = SymbolTradingState::LIVE;
        paper_trades = 0;
        paper_net_bps_sum = 0.0;
    }
};

// =============================================================================
// SCALP SIGNAL
// =============================================================================
struct ScalpSignal {
    int8_t direction = 0;
    double confidence = 0.0;
    double size = 0.0;
    const char* reason = "";
    bool is_exit = false;
    double realized_pnl = 0.0;
    double realized_pnl_bps = 0.0;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double spread_bps = 0.0;
    int64_t hold_ms = 0;
    MicroState micro_state = MicroState::IDLE;
    VetoReason veto_reason = VetoReason::NONE;

    bool shouldTrade() const { return direction != 0 && confidence >= 0.75; }
};

// =============================================================================
// SCALP POSITION
// =============================================================================
struct ScalpPosition {
    bool active = false;
    int8_t side = 0;
    double entry_price = 0;
    double size = 0;
    uint64_t entry_time_ns = 0;
    int64_t entry_time_ms = 0;
    int ticks_held = 0;
    double highest = 0, lowest = 0;
    double entry_spread_bps = 0;

    void open(int8_t s, double px, double sz, uint64_t ts, int64_t ms, double spread) {
        active = true; side = s; entry_price = px; size = sz;
        entry_time_ns = ts; entry_time_ms = ms;
        ticks_held = 0; highest = lowest = px;
        entry_spread_bps = spread;
    }
    void close() { active = false; side = 0; size = 0; }
    void update(double mid) { 
        ticks_held++; 
        highest = std::max(highest, mid); 
        lowest = std::min(lowest, mid); 
    }
    double pnlBps(double mid) const {
        if (!active || entry_price == 0) return 0;
        return (mid - entry_price) / entry_price * 10000.0 * side;
    }
};

// =============================================================================
// SYMBOL STATE - Extended for v4.2 AllowTradeHFT
// =============================================================================
struct SymbolState {
    double bid = 0, ask = 0, mid = 0, spread = 0;
    double ema_fast = 0, ema_slow = 0;
    double momentum = 0, micro_vol = 0, vwap = 0;
    double price_sum = 0;
    int price_count = 0;
    uint64_t ticks = 0;
    ScalpPosition pos;
    
    // v7.11: Extended tracking
    int64_t last_trade_ms = 0;
    int8_t last_trade_direction = 0;
    int64_t last_sl_exit_ms = 0;          // Track SL exits for revenge block
    int64_t last_time_entry_ms = 0;       // Track TIME entries for cooldown
    const char* last_exit_reason = "";
    
    // v7.11: Expectancy tracking
    ExpectancyTracker expectancy;
    
    // v4.2: HFT GATE TRACKING - Critical for edge-vs-cost invariant
    // Displacement tracking (chop detection)
    double price_min_window = 1e18;
    double price_max_window = 0.0;
    uint64_t displacement_window_start_ns = 0;
    static constexpr uint64_t DISPLACEMENT_WINDOW_NS = 500'000'000;  // 500ms
    
    // Volatility tracking (short-term realized vol)
    double vol_price_sum = 0.0;
    double vol_price_sum_sq = 0.0;
    uint64_t vol_sample_count = 0;
    double realized_vol_bps = 3.0;  // Start conservative for CFDs
    
    // Cooldown tracking
    uint64_t cooldown_until_ns = 0;
    uint64_t last_trade_ns = 0;
    
    // Block reason tracking (for diagnostics)
    uint64_t blocked_cost = 0;
    uint64_t blocked_chop = 0;
    uint64_t blocked_vol = 0;
    uint64_t blocked_cooldown = 0;
    uint64_t blocked_frequency = 0;
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: WIN RATE IMPROVEMENTS
    // ═══════════════════════════════════════════════════════════════════════
    
    // 1. Edge confirmation tracking (burst-relative)
    uint64_t edge_confirm_start_ns = 0;
    uint64_t burst_start_ns = 0;  // When current burst began
    uint64_t edge_confirm_ns = 50'000'000;  // Per-symbol, adaptive (default 50ms for CFD)
    
    // Confirmation bounds
    static constexpr uint64_t CFD_MIN_CONFIRM_NS = 30'000'000;   // 30ms
    static constexpr uint64_t CFD_MAX_CONFIRM_NS = 90'000'000;   // 90ms
    
    // 2. Micro-trend tracking (1s rolling direction)
    double micro_trend = 0.0;
    double micro_trend_ema = 0.0;
    
    // 3. Entry edge tracking (for edge-decay exit)
    double entry_edge_bps = 0.0;  // Edge at entry time
    
    // 4. Rolling win rate (for self-healing)
    int trades_today = 0;
    int wins_today = 0;
    bool disabled_for_day = false;
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: EXPECTANCY MEMORY - PnL-normalized EMA (converges in minutes)
    // Positive → confirms faster, Negative → demands stronger proof
    // ═══════════════════════════════════════════════════════════════════════
    double ema_expectancy = 0.0;     // PnL-normalized expectancy
    uint32_t exp_samples = 0;        // Sample count
    double avg_win_bps = 2.0;        // Running average win (for normalization)
    double sum_wins_bps = 0.0;       // Sum of winning trades
    static constexpr double EXP_ALPHA = 0.15;  // Fast but stable convergence
    
    // Update expectancy on trade close
    void update_expectancy(double trade_pnl_bps, bool is_win) {
        // Update average win for normalization
        if (is_win && trade_pnl_bps > 0) {
            sum_wins_bps += trade_pnl_bps;
            avg_win_bps = sum_wins_bps / std::max(1, wins_today);
        }
        
        // Normalize trade PnL by average win
        double normalized = (avg_win_bps > 0.1) ? trade_pnl_bps / avg_win_bps : trade_pnl_bps;
        
        if (exp_samples == 0) {
            ema_expectancy = normalized;
        } else {
            ema_expectancy = EXP_ALPHA * normalized + (1.0 - EXP_ALPHA) * ema_expectancy;
        }
        exp_samples++;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: CAPITAL ALLOCATION - Score-based budget distribution
    // ═══════════════════════════════════════════════════════════════════════
    double symbol_score = 1.0;       // Quality × Activity score
    double allocation = 1.0;         // Current allocation (0.0-1.0)
    double prev_allocation = 1.0;    // For hysteresis smoothing
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: LATENCY TRACKING - Per-symbol latency and slippage monitoring
    // ═══════════════════════════════════════════════════════════════════════
    LatencyStats latency;
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: KILL-SWITCH INTEGRATION - Safety system per symbol
    // ═══════════════════════════════════════════════════════════════════════
    KillSwitchStats kill_switch_stats;
    KillSwitchController kill_switch;
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: CAPITAL SCALING - Volatility-aware position sizing
    // ═══════════════════════════════════════════════════════════════════════
    double volatility_ema = 1.0;     // EMA of |trade_pnl| for stability
    double capital_weight = 1.0;     // Dynamic capital multiplier (0.5-3.0)
    static constexpr double VOL_ALPHA = 0.1;
    
    void update_volatility(double trade_pnl_bps) {
        volatility_ema = VOL_ALPHA * std::abs(trade_pnl_bps) + 
                         (1.0 - VOL_ALPHA) * volatility_ema;
    }
    
    void compute_capital_weight() {
        // Quality = expectancy signal
        double quality = std::clamp(1.0 + ema_expectancy, 0.5, 2.0);
        // Stability = inverse of volatility
        double stability = 1.0 / (1.0 + volatility_ema * 0.1);
        // Capital weight combines both
        capital_weight = std::clamp(quality * stability, 0.5, 3.0);
    }
    
    // Compute symbol score with microstructure bonus (call every 60s)
    void compute_score_with_micro(const MicrostructureProfile& mp) {
        // Quality = expectancy signal (clamped to 0.5-1.5)
        double quality = std::clamp(1.0 + ema_expectancy, 0.5, 1.5);
        
        // Efficiency = trades/confirms ratio
        double efficiency = 1.0;
        if (confirms_passed > 0) {
            efficiency = std::clamp(
                double(trades_fired + 1) / double(confirms_passed + 1),
                0.5, 1.5
            );
        }
        
        // Microstructure bonus = inverse of snapback penalty
        double micro_bonus = 1.0 - mp.snapback_penalty * 0.5;
        
        symbol_score = quality * efficiency * micro_bonus;
    }
    
    // Compute symbol score (call every 60s)
    void compute_score() {
        // Quality = expectancy signal (clamped to 0.5-1.5)
        double quality = std::clamp(1.0 + ema_expectancy, 0.5, 1.5);
        
        // Activity = confirm/burst ratio (how often bursts become opportunities)
        double activity = 1.0;
        if (bursts_detected > 0) {
            activity = std::clamp(
                double(confirms_passed + 1) / double(bursts_detected + 1),
                0.5, 1.5
            );
        }
        
        symbol_score = quality * activity;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: DIAGNOSTIC COUNTERS - For immediate failure mode identification
    // bursts_detected > confirms_passed → confirmation too strict
    // confirms_passed > trades_fired → downstream veto blocking
    // trades_fired ≈ confirms_passed → engine healthy
    // ═══════════════════════════════════════════════════════════════════════
    uint32_t bursts_detected = 0;
    uint32_t confirms_passed = 0;
    uint32_t trades_fired = 0;
    uint64_t last_stats_log_ns = 0;
    static constexpr uint64_t STATS_LOG_INTERVAL_NS = 60'000'000'000ULL;  // 60s
    
    // v4.2.2: Auto-blacklist for session
    double session_pnl_bps = 0.0;
    double session_avg_win_bps = 0.0;
    bool auto_blacklisted = false;
    
    void log_stats(const char* symbol, uint64_t now_ns) {
        if (now_ns - last_stats_log_ns > STATS_LOG_INTERVAL_NS) {
            last_stats_log_ns = now_ns;
            compute_score();  // Update score every minute
            compute_capital_weight();  // Update capital weight
            
            // Update kill-switch
            kill_switch_stats.updateLatency(latency);
            kill_switch.update(symbol, kill_switch_stats, now_ns);
            
            // Record latency sample for correlation analysis
            GetKillSwitchAnalytics().recordLatencySample(latency.ema_rtt_ms);
            
            // Check for recovery
            if (kill_switch.level() == KillSwitchLevel::NORMAL && 
                kill_switch.recoveryState() == RecoveryState::REARMED) {
                GetKillSwitchAnalytics().recordRecovery(now_ns);
            }
            
            std::cout << "[SCALP-STATS " << symbol << "] bursts=" << bursts_detected
                      << " confirms=" << confirms_passed
                      << " trades=" << trades_fired
                      << " exp=" << std::showpos << std::fixed << std::setprecision(2) << ema_expectancy << std::noshowpos
                      << " score=" << std::fixed << std::setprecision(2) << symbol_score
                      << " alloc=" << std::fixed << std::setprecision(2) << allocation
                      << " cap_wt=" << std::fixed << std::setprecision(2) << capital_weight
                      << " lat=" << std::fixed << std::setprecision(1) << latency.ema_rtt_ms << "ms"
                      << " ks=" << KillSwitchLevelStr(kill_switch.level())
                      << " pnl=" << session_pnl_bps << "bps\n";
        }
    }
    
    // v4.2.2: Auto-blacklist check (per session)
    // If net_pnl <= -3 × avg_win AND trade_count >= 3 → disable for session
    void check_auto_blacklist() {
        if (auto_blacklisted) return;
        if (trades_fired < 3) return;
        
        // Calculate average win
        double avg_win = wins_today > 0 ? session_pnl_bps / wins_today : 2.0;  // Default 2bps
        if (session_pnl_bps <= -3.0 * std::abs(avg_win)) {
            auto_blacklisted = true;
            std::cout << "[AUTO-BLACKLIST] Symbol auto-disabled: pnl=" << session_pnl_bps 
                      << " threshold=" << (-3.0 * std::abs(avg_win)) << "\n";
        }
    }
    
    // 5. Block reason tracking for diagnostics
    enum class BlockReason : uint8_t {
        NONE = 0,
        EDGE_CONFIRMING,
        NO_BURST,
        COST_TOO_HIGH,
        EDGE_TOO_LOW,
        COUNTER_TREND,
        CHOP,
        COOLDOWN,
        DISPLACEMENT_LOW,
        SPREAD_TOO_WIDE,
        RANGING,
        FREQUENCY,
        SESSION_GATE,
        EXPECTANCY_CONFIRM,
        KILL_SWITCH,
        LATENCY_HIGH
    };
    
    BlockReason last_block_reason = BlockReason::NONE;
    std::array<uint64_t, 18> block_counts = {};  // Indexed by BlockReason (extended for new reasons)
    
    void record_block(BlockReason reason) {
        last_block_reason = reason;
        size_t idx = static_cast<size_t>(reason);
        if (idx < block_counts.size()) {
            block_counts[idx]++;
        }
    }
    
    static const char* block_reason_str(BlockReason r) {
        switch (r) {
            case BlockReason::NONE: return "NONE";
            case BlockReason::EDGE_CONFIRMING: return "EDGE_CONFIRMING";
            case BlockReason::NO_BURST: return "NO_BURST";
            case BlockReason::COST_TOO_HIGH: return "COST_TOO_HIGH";
            case BlockReason::EDGE_TOO_LOW: return "EDGE_TOO_LOW";
            case BlockReason::COUNTER_TREND: return "COUNTER_TREND";
            case BlockReason::CHOP: return "CHOP";
            case BlockReason::COOLDOWN: return "COOLDOWN";
            case BlockReason::DISPLACEMENT_LOW: return "DISPLACEMENT_LOW";
            case BlockReason::SPREAD_TOO_WIDE: return "SPREAD_TOO_WIDE";
            case BlockReason::RANGING: return "RANGING";
            case BlockReason::FREQUENCY: return "FREQUENCY";
            case BlockReason::SESSION_GATE: return "SESSION_GATE";
            case BlockReason::EXPECTANCY_CONFIRM: return "EXPECTANCY_CONFIRM";
            case BlockReason::KILL_SWITCH: return "KILL_SWITCH";
            case BlockReason::LATENCY_HIGH: return "LATENCY_HIGH";
            default: return "UNKNOWN";
        }
    }
    
    double rolling_winrate() const {
        return trades_today > 0 ? double(wins_today) / trades_today : 0.5;
    }

    void init(double b, double a) {
        bid = b; ask = a; mid = (b + a) / 2; spread = a - b;
        ema_fast = ema_slow = vwap = mid;
        price_sum = mid; price_count = 1;
        micro_vol = 0.0001; ticks = 1;
        last_trade_ms = 0;
        last_trade_direction = 0;
        last_sl_exit_ms = 0;
        last_time_entry_ms = 0;
        last_exit_reason = "";
        // v4.2: Init HFT tracking
        price_min_window = mid;
        price_max_window = mid;
        displacement_window_start_ns = 0;
        // v4.2.2: Init win-rate tracking
        edge_confirm_start_ns = 0;
        burst_start_ns = 0;
        micro_trend = 0.0;
        micro_trend_ema = 0.0;
        trades_today = 0;
        wins_today = 0;
        disabled_for_day = false;
        last_block_reason = BlockReason::NONE;
        block_counts.fill(0);
        // v4.2.2: Init expectancy memory
        ema_expectancy = 0.0;
        exp_samples = 0;
        avg_win_bps = 2.0;
        sum_wins_bps = 0.0;
        // v4.2.2: Init capital allocation
        symbol_score = 1.0;
        allocation = 1.0;
        prev_allocation = 1.0;
        // v4.2.2: Init latency tracking
        latency.reset();
        // v4.2.2: Init kill-switch
        kill_switch_stats.reset();
        // v4.2.2: Init capital scaling
        volatility_ema = 1.0;
        capital_weight = 1.0;
        // v4.2.2: Init diagnostic counters
        bursts_detected = 0;
        confirms_passed = 0;
        trades_fired = 0;
        last_stats_log_ns = 0;
        session_pnl_bps = 0.0;
        session_avg_win_bps = 0.0;
        auto_blacklisted = false;
    }

    void update(double b, double a) {
        double prev_mid = mid;
        bid = b; ask = a; mid = (b + a) / 2; spread = a - b;

        ema_fast = 0.3 * mid + 0.7 * ema_fast;
        ema_slow = 0.1 * mid + 0.9 * ema_slow;

        double chg = mid - prev_mid;
        momentum = 0.3 * chg + 0.7 * momentum;
        micro_vol = 0.15 * std::fabs(chg) + 0.85 * micro_vol;
        if (micro_vol < 0.00001) micro_vol = 0.00001;

        price_sum += mid; price_count++;
        if (price_count > 20) { price_sum = vwap * 19 + mid; price_count = 20; }
        vwap = price_sum / price_count;

        ticks++;
        if (pos.active) pos.update(mid);
        
        // v4.2: Update volatility tracking
        updateRealizedVol(mid);
        
        // v4.2.2: Update micro-trend (1s EMA of direction)
        micro_trend_ema = 0.05 * chg + 0.95 * micro_trend_ema;  // ~1s at 50Hz
        micro_trend = (micro_trend_ema > 0) ? 1.0 : (micro_trend_ema < 0) ? -1.0 : 0.0;
    }
    
    // v4.2: Update displacement window
    void updateDisplacement(uint64_t now_ns) {
        if (now_ns - displacement_window_start_ns > DISPLACEMENT_WINDOW_NS) {
            displacement_window_start_ns = now_ns;
            price_min_window = mid;
            price_max_window = mid;
        } else {
            price_min_window = std::min(price_min_window, mid);
            price_max_window = std::max(price_max_window, mid);
        }
    }
    
    // v4.2: Get displacement in bps
    double getDisplacementBps() const {
        if (price_max_window <= 0 || price_min_window >= 1e17) return 0.0;
        double m = (price_max_window + price_min_window) / 2.0;
        if (m <= 0) return 0.0;
        return (price_max_window - price_min_window) / m * 10000.0;
    }
    
    // v4.2: Update realized volatility
    void updateRealizedVol(double price) {
        vol_sample_count++;
        vol_price_sum += price;
        vol_price_sum_sq += price * price;
        
        // v4.2.2: Reduced from 50 to 10 samples (CFD markets are sparse)
        if (vol_sample_count >= 10) {
            double mean = vol_price_sum / vol_sample_count;
            double variance = (vol_price_sum_sq / vol_sample_count) - (mean * mean);
            if (variance > 0 && mean > 0) {
                double stddev = std::sqrt(variance);
                realized_vol_bps = (stddev / mean) * 10000.0;
            }
            
            // Rolling window decay
            if (vol_sample_count >= 200) {
                vol_price_sum *= 0.5;
                vol_price_sum_sq *= 0.5;
                vol_sample_count = 100;
            }
        }
    }

    double spreadBps() const { return mid > 0 ? (spread / mid) * 10000.0 : 9999; }
    
    int8_t trend() const { 
        return (ema_fast > ema_slow && momentum > 0) ? 1 :
               (ema_fast < ema_slow && momentum < 0) ? -1 : 0; 
    }

    MicroInputs toMicroInputs(int64_t ts) const {
        return {mid, vwap, micro_vol, spreadBps(), ts};
    }
};

// =============================================================================
// AllowTradeHFT - THE GATE (same logic for all CFDs, different numbers)
// v4.2.2: GOLDEN RULE - Trade EXISTENCE on RAW edge. Sizing on scaled edge.
// v4.2.2: WIN RATE IMPROVEMENTS - Edge confirmation, regime filter, directional bias
// v4.2.2: MICROSTRUCTURE PROFILES - Per-symbol burst/confirm parameters
// =============================================================================
inline bool AllowTradeHFT(
    SymbolState& st,
    const HFTProfile& profile,
    uint64_t now_ns,
    const char*& block_reason,
    const char* symbol = "",  // v4.2.2: For per-venue logic
    int8_t intended_direction = 0  // v4.2.2: For directional bias check
) {
    // v4.2.2: Get microstructure profile for this symbol
    const MicrostructureProfile& mp = GetMicrostructureProfile(symbol);
    
    // v4.2.2: Periodic stats logging (includes kill-switch update)
    st.log_stats(symbol, now_ns);
    
    double spread_bps = st.spreadBps();
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: KILL-SWITCH CHECK - Safety system overrides everything
    // ═══════════════════════════════════════════════════════════════════════
    if (!st.kill_switch.canTrade()) {
        block_reason = "KILL_SWITCH";
        st.record_block(SymbolState::BlockReason::KILL_SWITCH);
        
        // Record kill analytics
        GetKillSwitchAnalytics().recordKill(
            symbol, 
            st.kill_switch.reason(), 
            st.latency.ema_rtt_ms, 
            now_ns
        );
        
        // Prometheus counter
        METRIC_INC(kill_switch_triggers);
        METRIC_INC(blocks_total);
        
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: LATENCY CHECK - Block if latency too high for this symbol
    // ═══════════════════════════════════════════════════════════════════════
    double max_latency_ms = 20.0 * (1.0 - mp.latency_sensitivity);
    if (st.latency.ema_rtt_ms > max_latency_ms) {
        block_reason = "LATENCY_HIGH";
        st.record_block(SymbolState::BlockReason::LATENCY_HIGH);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: HARD BLACKLIST - Structurally incompatible symbols
    // USDJPY: 93% of session loss, FIX batching + pip value asymmetry
    // ═══════════════════════════════════════════════════════════════════════
    if (IsSymbolBlacklisted(symbol)) {
        block_reason = "SYMBOL_BLACKLISTED";
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: AUTO-BLACKLIST - Session protection
    // If net_pnl <= -3 × avg_win AND trade_count >= 3 → disable for session
    // ═══════════════════════════════════════════════════════════════════════
    if (st.auto_blacklisted) {
        block_reason = "AUTO_BLACKLISTED";
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: WIN RATE GATE 1 - SYMBOL SELF-HEALING
    // If rolling win rate < 40% after 5 trades today, disable symbol
    // ═══════════════════════════════════════════════════════════════════════
    if (st.disabled_for_day) {
        block_reason = "SYMBOL_DISABLED_DAY";
        return false;
    }
    
    if (st.trades_today >= 5 && st.rolling_winrate() < 0.40) {
        st.disabled_for_day = true;
        block_reason = "SYMBOL_DISABLED_DAY";
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // BOOTSTRAP RELAXATION - allows system to seed expectancy
    // CFD bootstrap: edge_mult=1.4, min_edge=1.5 (more conservative than crypto)
    // ═══════════════════════════════════════════════════════════════════════
    static constexpr int BOOTSTRAP_TRADES = 15;  // CFD needs less for bootstrap
    bool bootstrap = (st.expectancy.count < BOOTSTRAP_TRADES);
    
    double effective_edge_mult = profile.min_edge_mult * mp.burst_threshold_mult;
    double effective_min_edge = profile.min_edge_bps;
    
    if (bootstrap) {
        // CFD bootstrap relaxation (more conservative than crypto)
        effective_edge_mult = 1.4;
        effective_min_edge = 1.5;
    }
    
    // 1. SPREAD SANITY
    if (spread_bps <= 0.0 || spread_bps > profile.max_spread_bps) {
        block_reason = "SPREAD_WIDE";
        st.record_block(SymbolState::BlockReason::SPREAD_TOO_WIDE);
        st.burst_start_ns = 0;  // Reset burst
        st.edge_confirm_start_ns = 0;
        return false;
    }
    
    // 2. TOTAL COST CALCULATION
    double total_cost_bps = spread_bps + profile.slippage_bps + 0.5;  // +0.5 safety
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: CRITICAL FIX - Compute RAW edge for gating
    // Vol cap applied ONLY for sizing, NOT for existence decision
    // ═══════════════════════════════════════════════════════════════════════
    double raw_edge_bps = std::abs(st.momentum) / st.mid * 10000.0 * 10;  // 10-tick projection
    
    // EDGE STARVATION DETECTION - signals never updated
    if (raw_edge_bps < 0.01) {
        block_reason = "EDGE_STARVED";
        st.record_block(SymbolState::BlockReason::EDGE_TOO_LOW);
        return false;
    }
    
    // DIAGNOSTIC: Log edge values periodically (every 100 blocks)
    static thread_local uint64_t edge_log_counter = 0;
    if (++edge_log_counter % 500 == 1) {
        std::cout << "[EDGE " << symbol << "] raw=" << raw_edge_bps 
                  << " min=" << effective_min_edge
                  << " cost=" << total_cost_bps
                  << " disp=" << st.getDisplacementBps()
                  << " mom=" << st.momentum
                  << " trend=" << st.micro_trend_ema << "\n";
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // HARD GATES ON RAW EDGE (NOT vol-capped edge)
    // v4.2.2: Trade EXISTENCE decided here - NO scaling applied yet
    // ═══════════════════════════════════════════════════════════════════════
    
    // HYSTERESIS: Only reset burst if edge COLLAPSES, not minor dip
    // This prevents single-tick noise from killing confirmation timer
    static constexpr double EDGE_RESET_RATIO = 0.6;  // Reset only if edge < 60% of min
    
    // 3. ABSOLUTE EDGE FLOOR (RAW edge) - WITH HYSTERESIS
    if (raw_edge_bps < effective_min_edge * EDGE_RESET_RATIO) {
        // Edge COLLAPSED - hard reset
        block_reason = "LOW_EDGE";
        st.blocked_cost++;
        st.record_block(SymbolState::BlockReason::EDGE_TOO_LOW);
        if (st.burst_start_ns != 0) {
            std::cout << "[BURST] RESET (edge collapsed to " << raw_edge_bps << " bps)\n";
        }
        st.burst_start_ns = 0;
        st.edge_confirm_start_ns = 0;
        return false;
    }
    
    // Edge below min but above reset threshold - block but DON'T reset timer
    if (raw_edge_bps < effective_min_edge) {
        block_reason = "LOW_EDGE";
        st.record_block(SymbolState::BlockReason::EDGE_TOO_LOW);
        // NO RESET - edge may recover on next tick
        return false;
    }
    
    // 4. HARD EDGE VS COST (RAW edge - THE INVARIANT)
    if (raw_edge_bps < total_cost_bps * effective_edge_mult) {
        block_reason = "EDGE_LT_COST";
        st.blocked_cost++;
        st.record_block(SymbolState::BlockReason::COST_TOO_HIGH);
        // Only reset if significantly below threshold
        if (raw_edge_bps < total_cost_bps * effective_edge_mult * EDGE_RESET_RATIO) {
            if (st.burst_start_ns != 0) {
                std::cout << "[BURST] RESET (edge << cost)\n";
            }
            st.burst_start_ns = 0;
            st.edge_confirm_start_ns = 0;
        }
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: WIN RATE GATE 2 - BURST-RELATIVE EDGE CONFIRMATION
    // Confirmation measured from burst start, not absolute time
    // Required = min(edge_confirm_ns, burst_age * 70%)
    // This allows late-burst entries while still filtering flickers
    // ═══════════════════════════════════════════════════════════════════════
    
    // Latch burst start (edge + cost gates passed = burst active)
    if (st.burst_start_ns == 0) {
        st.burst_start_ns = now_ns;
        st.bursts_detected++;  // v4.2.2: Diagnostic counter
        METRIC_INC(bursts_detected);  // Prometheus
        std::cout << "[BURST] START detected (total=" << st.bursts_detected << ")\n";
    }
    
    // Edge confirmation starts at burst start, not now
    if (st.edge_confirm_start_ns == 0) {
        st.edge_confirm_start_ns = st.burst_start_ns;
    }
    
    uint64_t burst_age_ns = now_ns - st.burst_start_ns;
    uint64_t confirm_age_ns = now_ns - st.edge_confirm_start_ns;
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: SESSION STATE DETECTION
    // OPEN: burst-first, conservative sizing
    // ACTIVE: full allocation
    // FADE: no new entries
    // ═══════════════════════════════════════════════════════════════════════
    SessionState session_state = GetSessionState();
    [[maybe_unused]] bool session_open_mode = (session_state == SessionState::OPEN);
    
    // FADE session = no new entries
    if (session_state == SessionState::FADE) {
        block_reason = "SESSION_FADE";
        st.record_block(SymbolState::BlockReason::COOLDOWN);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: MICROSTRUCTURE-PROFILE-AWARE CONFIRMATION
    // Uses per-symbol parameters from MicrostructureProfile
    // ═══════════════════════════════════════════════════════════════════════
    uint64_t min_burst_age_ns = static_cast<uint64_t>(mp.min_burst_age_ms * 1'000'000.0);
    uint64_t confirm_pct = static_cast<uint64_t>(mp.confirm_pct * 100.0);
    uint64_t required_confirm_ns = std::min(st.edge_confirm_ns, burst_age_ns * confirm_pct / 100);
    
    // Enforce minimum burst age from profile
    if (required_confirm_ns < min_burst_age_ns) required_confirm_ns = min_burst_age_ns;
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: EXPECTANCY-WEIGHTED CONFIRMATION (CORE EDGE)
    // Positive expectancy → confirms faster (reward)
    // Negative expectancy → demands stronger proof (protect)
    // Neutral → unchanged
    // ═══════════════════════════════════════════════════════════════════════
    double exp_factor = std::clamp(1.0 - st.ema_expectancy * 0.3, 0.5, 1.5);
    // Note: Positive exp → lower factor → faster confirm
    //       Negative exp → higher factor → slower confirm
    
    // Apply snapback penalty from microstructure profile
    double snapback_adj = 1.0 + mp.snapback_penalty * 0.3;
    exp_factor *= snapback_adj;
    exp_factor = std::clamp(exp_factor, 0.5, 2.0);
    
    uint64_t adj_confirm_ns = static_cast<uint64_t>(required_confirm_ns * exp_factor);
    
    if (confirm_age_ns < adj_confirm_ns) {
        block_reason = "EDGE_CONFIRMING";
        st.record_block(SymbolState::BlockReason::EDGE_CONFIRMING);
        uint64_t confirm_age_ms = confirm_age_ns / 1'000'000;
        uint64_t adj_ms = adj_confirm_ns / 1'000'000;
        uint64_t burst_age_ms = burst_age_ns / 1'000'000;
        if (confirm_age_ms > 0) {
            std::cout << "[EDGE-CONFIRM] waiting " << confirm_age_ms << "ms / " 
                      << adj_ms << "ms (burst " << burst_age_ms << "ms)"
                      << " exp_factor=" << std::fixed << std::setprecision(2) << exp_factor 
                      << " snapback=" << mp.snapback_penalty << "\n";
        }
        return false;
    }
    
    // v4.2.2: Diagnostic counter - confirmation passed
    st.confirms_passed++;
    METRIC_INC(confirms_passed);  // Prometheus
    std::cout << "[EDGE-CONFIRM] ✓ PASSED after " << (confirm_age_ns / 1'000'000) 
              << "ms (burst " << (burst_age_ns / 1'000'000) << "ms)"
              << " exp=" << std::showpos << std::fixed << std::setprecision(2) << st.ema_expectancy << std::noshowpos
              << " profile=" << mp.min_burst_age_ms << "ms/" << (mp.confirm_pct * 100) << "%"
              << " [confirms=" << st.confirms_passed << "]\n";
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: MICROSTRUCTURE-AWARE DISPLACEMENT CHECK
    // Uses min_displacement_atr from profile
    // ═══════════════════════════════════════════════════════════════════════
    double displacement = st.getDisplacementBps();
    double atr_1s = st.realized_vol_bps * 10.0;  // Approximate 1s ATR from vol
    double min_displacement = mp.min_displacement_atr * atr_1s;
    
    // Per-venue chop floor - FX needs much lower threshold
    // Detect asset class from symbol name
    bool is_fx = (strstr(symbol, "USD") != nullptr && strstr(symbol, "XA") == nullptr) ||
                 strstr(symbol, "EUR") != nullptr || strstr(symbol, "GBP") != nullptr ||
                 strstr(symbol, "JPY") != nullptr || strstr(symbol, "CHF") != nullptr ||
                 strstr(symbol, "CAD") != nullptr || strstr(symbol, "AUD") != nullptr ||
                 strstr(symbol, "NZD") != nullptr;
    bool is_metal = (strstr(symbol, "XAU") != nullptr || strstr(symbol, "XAG") != nullptr);
    bool is_index = (strstr(symbol, "US30") != nullptr || strstr(symbol, "US100") != nullptr ||
                     strstr(symbol, "NAS") != nullptr || strstr(symbol, "SPX") != nullptr ||
                     strstr(symbol, "GER") != nullptr || strstr(symbol, "UK100") != nullptr);
    
    double chop_mult = is_fx    ? 0.8 :   // FX: tight spreads, small moves
                       is_metal ? 1.2 :   // Metals: moderate
                       is_index ? 1.0 :   // Indices: standard
                                  1.2;    // Default
    
    double chop_floor = std::max(spread_bps * chop_mult, std::max(min_displacement, is_fx ? 0.5 : 1.0));
    if (displacement < chop_floor) {
        block_reason = "CHOP";
        st.blocked_chop++;
        st.record_block(SymbolState::BlockReason::CHOP);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: WIN RATE GATE 3 - RANGING HARD KILL (per-venue)
    // RANGING + low displacement = hard no-trade zone
    // But FX/metals have naturally lower displacement
    // ═══════════════════════════════════════════════════════════════════════
    bool is_ranging = (st.ema_fast > st.ema_slow * 0.9999 && 
                       st.ema_fast < st.ema_slow * 1.0001);  // EMA convergence
    
    double ranging_mult = is_fx    ? 1.5 :   // FX: lower threshold
                          is_metal ? 2.0 :   // Metals: moderate
                          is_index ? 1.8 :   // Indices: moderate
                                     2.5;    // Crypto: standard
    
    if (is_ranging && displacement < spread_bps * ranging_mult) {
        block_reason = "RANGING_CHOP";
        st.record_block(SymbolState::BlockReason::RANGING);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: WIN RATE GATE 4 - DIRECTIONAL BIAS FILTER (DAMPENED)
    // Only block if micro-trend is STRONG (|ema| > 0.8)
    // Allows mean-reversion scalps in normal conditions
    // Win rate gain: +5-8%
    // ═══════════════════════════════════════════════════════════════════════
    if (intended_direction != 0 && std::abs(st.micro_trend_ema) > 0.8) {
        // Block trades that fight STRONG micro-trend
        if ((st.micro_trend > 0 && intended_direction < 0) ||
            (st.micro_trend < 0 && intended_direction > 0)) {
            block_reason = "COUNTER_TREND";
            st.record_block(SymbolState::BlockReason::COUNTER_TREND);
            return false;
        }
    }
    
    // 6. COOLDOWN CHECK (after loss)
    if (now_ns < st.cooldown_until_ns) {
        block_reason = "COOLDOWN";
        st.blocked_cooldown++;
        st.record_block(SymbolState::BlockReason::COOLDOWN);
        return false;
    }
    
    // 7. TRADE FREQUENCY LIMIT
    if (now_ns - st.last_trade_ns < profile.min_time_between_trades_ns) {
        block_reason = "FREQUENCY";
        st.blocked_frequency++;
        st.record_block(SymbolState::BlockReason::FREQUENCY);
        return false;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // POST-GATE: Vol cap only affects sizing/TP, NOT existence
    // (Sizing logic is handled by caller using final_edge)
    // ═══════════════════════════════════════════════════════════════════════
    
    // v4.2.2: Diagnostic counter - trade allowed (gate passed)
    st.trades_fired++;
    METRIC_INC(trades_fired);  // Prometheus
    std::cout << "[TRADE-ALLOWED] " << symbol << " trade #" << st.trades_fired 
              << " (bursts=" << st.bursts_detected 
              << " confirms=" << st.confirms_passed << ")\n";
    
    // Reset burst and edge confirmation on success (for next trade)
    st.edge_confirm_start_ns = 0;
    st.burst_start_ns = 0;
    
    return true;
}

// =============================================================================
// BLOCK REPORT - Dump per-symbol trade blocking summary
// =============================================================================
inline void dump_block_report(const SymbolState& st, const std::string& symbol) {
    uint64_t total = 0;
    for (size_t i = 0; i < st.block_counts.size(); ++i) {
        total += st.block_counts[i];
    }
    
    if (total == 0) return;
    
    std::cout << "\n════════════════════════════════════════════════════════════\n";
    std::cout << "BLOCK REPORT: " << symbol << "\n";
    std::cout << "════════════════════════════════════════════════════════════\n";
    
    for (size_t i = 1; i < st.block_counts.size(); ++i) {  // Skip NONE
        if (st.block_counts[i] > 0) {
            double pct = double(st.block_counts[i]) / total * 100.0;
            std::cout << "  " << std::left << std::setw(18) 
                      << SymbolState::block_reason_str(static_cast<SymbolState::BlockReason>(i))
                      << " : " << st.block_counts[i] 
                      << " (" << std::fixed << std::setprecision(1) << pct << "%)\n";
        }
    }
    
    std::cout << "  TOTAL BLOCKS      : " << total << "\n";
    std::cout << "  TRADES TAKEN      : " << st.trades_today 
              << " (W:" << st.wins_today << " L:" << (st.trades_today - st.wins_today) << ")\n";
    if (st.trades_today > 0) {
        std::cout << "  WIN RATE          : " << std::fixed << std::setprecision(1) 
                  << (st.rolling_winrate() * 100.0) << "%\n";
    }
    std::cout << "  EDGE_CONFIRM_NS   : " << (st.edge_confirm_ns / 1'000'000) << "ms\n";
    std::cout << "  EXPECTANCY EMA    : " << std::showpos << std::fixed << std::setprecision(3) 
              << st.ema_expectancy << std::noshowpos << "\n";
    std::cout << "  SYMBOL SCORE      : " << std::fixed << std::setprecision(2) << st.symbol_score << "\n";
    std::cout << "  ALLOCATION        : " << std::fixed << std::setprecision(2) << st.allocation << "\n";
    std::cout << "════════════════════════════════════════════════════════════\n\n";
}

// =============================================================================
// SCORE-BASED ALLOCATOR - Score-based budget distribution across symbols
// Allocates capital to what's working now, starves what isn't
// (Separate from the full CapitalAllocator in risk/CapitalAllocator.hpp)
// =============================================================================
class ScoreBasedAllocator {
public:
    static constexpr size_t MAX_SYMBOLS = 30;
    static constexpr double MIN_ALLOC = 0.1;   // 10% minimum per symbol
    static constexpr double MAX_ALLOC = 0.4;   // 40% maximum per symbol
    static constexpr double SMOOTH_ALPHA = 0.3; // Allocation smoothing
    
    struct SymbolAlloc {
        const char* symbol = "";
        SymbolState* state = nullptr;
        double score = 1.0;
        double allocation = 1.0;
        bool enabled = true;
    };
    
private:
    std::array<SymbolAlloc, MAX_SYMBOLS> symbols_;
    size_t count_ = 0;
    double total_budget_ = 1.0;
    uint64_t last_recompute_ns_ = 0;
    static constexpr uint64_t RECOMPUTE_INTERVAL_NS = 60'000'000'000ULL;  // 60s
    
public:
    void registerSymbol(const char* symbol, SymbolState* state) {
        if (count_ >= MAX_SYMBOLS) return;
        symbols_[count_].symbol = symbol;
        symbols_[count_].state = state;
        symbols_[count_].enabled = !IsSymbolBlacklisted(symbol);
        count_++;
    }
    
    void setTotalBudget(double budget) { total_budget_ = budget; }
    
    // Recompute allocations (call every tick, internally throttled)
    void recompute(uint64_t now_ns) {
        if (now_ns - last_recompute_ns_ < RECOMPUTE_INTERVAL_NS) return;
        last_recompute_ns_ = now_ns;
        
        SessionState ss = GetSessionState();
        double session_mult = SessionRiskMultiplier(ss);
        
        // Step 1: Compute scores for enabled symbols
        double sum_scores = 0.0;
        for (size_t i = 0; i < count_; i++) {
            auto& sa = symbols_[i];
            if (!sa.enabled || !sa.state) continue;
            if (sa.state->auto_blacklisted || sa.state->disabled_for_day) {
                sa.enabled = false;
                continue;
            }
            
            sa.state->compute_score();
            sa.score = sa.state->symbol_score;
            sum_scores += sa.score;
        }
        
        if (sum_scores <= 0.001) sum_scores = 1.0;  // Avoid div by zero
        
        // Step 2: Normalize and clamp allocations
        for (size_t i = 0; i < count_; i++) {
            auto& sa = symbols_[i];
            if (!sa.enabled || !sa.state) {
                sa.allocation = 0.0;
                continue;
            }
            
            // Raw allocation proportional to score
            double raw_alloc = total_budget_ * (sa.score / sum_scores);
            
            // Clamp to [MIN_ALLOC, MAX_ALLOC]
            double clamped = std::clamp(raw_alloc, MIN_ALLOC, MAX_ALLOC);
            
            // Apply session overlay
            clamped *= session_mult;
            
            // Smooth with previous (anti-thrash)
            double prev = sa.state->prev_allocation;
            double smoothed = SMOOTH_ALPHA * clamped + (1.0 - SMOOTH_ALPHA) * prev;
            
            sa.allocation = smoothed;
            sa.state->prev_allocation = sa.state->allocation;
            sa.state->allocation = smoothed;
        }
        
        // Step 3: Log allocation summary
        std::cout << "[ALLOC] ";
        for (size_t i = 0; i < count_; i++) {
            auto& sa = symbols_[i];
            if (sa.enabled && sa.allocation > 0.01) {
                std::cout << sa.symbol << "=" << std::fixed << std::setprecision(2) 
                          << sa.allocation << " ";
            }
        }
        std::cout << "| session=" << SessionStateStr(ss) << "\n";
    }
    
    // Get allocation for a symbol (returns 0.0-1.0)
    double getAllocation(const char* symbol) const {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(symbols_[i].symbol, symbol) == 0) {
                return symbols_[i].allocation;
            }
        }
        return 1.0;  // Default full allocation for unknown
    }
    
    // Rank symbols by score for execution priority
    void getRankedSymbols(std::vector<const char*>& out) const {
        out.clear();
        
        // Copy enabled symbols
        std::vector<std::pair<double, const char*>> scored;
        for (size_t i = 0; i < count_; i++) {
            if (symbols_[i].enabled && symbols_[i].allocation > 0.01) {
                scored.emplace_back(symbols_[i].score, symbols_[i].symbol);
            }
        }
        
        // Sort descending by score
        std::sort(scored.begin(), scored.end(), 
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        
        for (const auto& s : scored) {
            out.push_back(s.second);
        }
    }
};

// =============================================================================
// PURE SCALPER v7.11 - Main class
// =============================================================================
class PureScalper {
public:
    struct Config {
        double size = 0.01;
        double contract_size = 100.0;
        int warmup = 10;  // v4.2.2: Reduced from 50 (CFD markets are sparse)
        bool debug = true;
    };

    PureScalper() {
        microMgr_.setDebugSymbol("XAUUSD");
        microMgr_.setSimpleMode(true);
    }

    void setConfig(const Config& c) { cfg_ = c; }
    Config& getConfig() { return cfg_; }

    void enableDebug(const std::string& symbol) {
        microMgr_.setDebugSymbol(symbol);
    }

    // =========================================================================
    // MAIN PROCESS - Symbol-specific execution
    // =========================================================================
    ScalpSignal process(const char* symbol, double bid, double ask, double, double, uint64_t ts) {
        ScalpSignal sig;
        std::string sym(symbol);
        int64_t now_ms = getNowMs();

        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: AUTO-DISABLE SYMBOL HEALTH CHECK (FIRST GATE)
        // If symbol is unhealthy (10+ trades, <35% WR), disable it
        // ═══════════════════════════════════════════════════════════════════
        if (!Chimera::symbol_enabled(sym)) {
            sig.reason = "SYMBOL_DISABLED";
            Chimera::record_block(sym, Chimera::BlockReason::SYMBOL_DISABLED);
            return sig;
        }

        // Get symbol-specific profile
        SymbolProfile profile = getSymbolProfile(sym);

        // Init or update state
        auto& st = states_[sym];
        if (st.ticks == 0) {
            st.init(bid, ask);
            
            // v4.2.2: Set per-symbol confirmation window based on asset class
            // FX: slower ticks, need longer confirmation
            // Metals: even slower, need more time
            // Indices: fast, but still quote-based
            if (sym == "XAUUSD" || sym == "XAGUSD") {
                st.edge_confirm_ns = 60'000'000;  // 60ms for metals
            } else if (sym == "NAS100" || sym == "US100" || sym == "US30" || 
                       sym == "SPX500" || sym == "GER40") {
                st.edge_confirm_ns = 40'000'000;  // 40ms for indices
            } else {
                st.edge_confirm_ns = 50'000'000;  // 50ms for FX
            }
            
            sig.reason = "INIT";
            return sig;
        }
        st.update(bid, ask);

        // Warmup
        if (st.ticks < (uint64_t)cfg_.warmup) {
            sig.reason = "WARMUP";
            Chimera::record_block(sym, Chimera::BlockReason::WARMUP);
            return sig;
        }

        // Get micro state machine
        auto& micro = microMgr_.get(sym);
        micro.onTick(st.toMicroInputs(ts));

        sig.micro_state = micro.state();
        sig.veto_reason = micro.lastVeto();
        double sprdBps = st.spreadBps();

        // =================================================================
        // GATE 1: EXPECTANCY AUTO-DISABLE
        // =================================================================
        if (st.expectancy.disabled) {
            sig.reason = st.expectancy.disable_reason;
            Chimera::record_block(sym, Chimera::BlockReason::NEG_EXPECTANCY);
            return sig;
        }

        // =================================================================
        // GATE 2: SESSION WINDOW (NAS100, US30)
        // =================================================================
        if (profile.session_gated && !isInSessionWindow(profile)) {
            sig.reason = "SESSION_BLOCK";
            Chimera::record_block(sym, Chimera::BlockReason::SESSION_CLOSED);
            return sig;
        }

        // =================================================================
        // POSITION MANAGEMENT - If holding, only check exits
        // =================================================================
        if (st.pos.active) {
            const HFTProfile& hft = ResolveHFTProfile(sym);
            sig = checkExit(st, ts, micro, now_ms, profile, hft);
            if (sig.direction != 0 && sig.is_exit) {
                // Record exit for tracking
                st.last_trade_ms = now_ms;
                st.last_trade_direction = sig.direction;
                st.last_exit_reason = sig.reason;
                
                // v4.2: Activate cooldown on ANY loss (SL, TIME with negative PnL)
                bool is_loss = sig.realized_pnl_bps < 0;
                bool is_win = sig.realized_pnl_bps > 0;
                if (is_loss) {
                    st.cooldown_until_ns = ts + hft.cooldown_after_loss_ns;
                    st.last_sl_exit_ms = now_ms;  // Legacy tracking
                }
                
                // v4.2.2: Track rolling win rate for self-healing
                st.trades_today++;
                if (is_win) st.wins_today++;
                
                // v4.2.2: Update expectancy EMA for adaptive confirmation
                st.update_expectancy(sig.realized_pnl_bps, is_win);
                st.session_pnl_bps += sig.realized_pnl_bps;
                st.check_auto_blacklist();  // May disable symbol if bleeding
                
                // v4.2.2: Update volatility for capital scaling
                st.update_volatility(sig.realized_pnl_bps);
                
                // v4.2.2: Update kill-switch stats
                st.kill_switch_stats.addPnL(sig.realized_pnl_bps);
                if (is_loss) {
                    st.kill_switch_stats.recordLoss();
                } else if (is_win) {
                    st.kill_switch_stats.recordWin();
                }
                
                // v4.2.2: Record to SymbolHealth for auto-disable logic
                Chimera::record_trade(sym, is_win, sig.realized_pnl_bps);
                
                // Record in expectancy tracker
                int64_t hold_ms = now_ms - st.pos.entry_time_ms;
                st.expectancy.recordTrade(sig.realized_pnl_bps, st.pos.entry_spread_bps, hold_ms);
                sig.hold_ms = hold_ms;
                sig.spread_bps = st.pos.entry_spread_bps;
                
                micro.onExit(ts);
                
                // v4.2.2: LOUD WIN/LOSS LOGGING - ALWAYS SHOW
                if (is_win) {
                    std::cout << "\n"
                              << "████████████████████████████████████████████████████████████\n"
                              << "██  ✅ WIN ✅  " << symbol << "  +" << std::fixed << std::setprecision(2) << sig.realized_pnl_bps << " bps\n"
                              << "██  reason=" << sig.reason << "  hold=" << hold_ms << "ms\n"
                              << "██  WR=" << std::setprecision(0) << (st.rolling_winrate() * 100) << "%  (" << st.wins_today << "W/" << (st.trades_today - st.wins_today) << "L)"
                              << "  exp=" << std::showpos << std::setprecision(2) << st.ema_expectancy << std::noshowpos << "\n"
                              << "████████████████████████████████████████████████████████████\n\n";
                } else if (is_loss) {
                    std::cout << "\n"
                              << "################################################################\n"
                              << "##  ❌ LOSS ❌  " << symbol << "  " << std::fixed << std::setprecision(2) << sig.realized_pnl_bps << " bps\n"
                              << "##  reason=" << sig.reason << "  hold=" << hold_ms << "ms  [COOLDOWN]\n"
                              << "##  WR=" << std::setprecision(0) << (st.rolling_winrate() * 100) << "%  (" << st.wins_today << "W/" << (st.trades_today - st.wins_today) << "L)"
                              << "  exp=" << std::showpos << std::setprecision(2) << st.ema_expectancy << std::noshowpos << "\n"
                              << "################################################################\n\n";
                } else {
                    // Scratch (near-zero PnL)
                    std::cout << "[SCRATCH] " << symbol << " " << sig.reason 
                              << " pnl=" << std::fixed << std::setprecision(2) << sig.realized_pnl_bps << "bps"
                              << " hold=" << hold_ms << "ms\n";
                }
                std::cout.flush();
            }
            Chimera::record_block(sym, Chimera::BlockReason::POSITION_OPEN);
            return sig;  // CRITICAL: No fall-through to entry logic
        }

        // =================================================================
        // GATE 3: v4.2 AllowTradeHFT - THE CRITICAL GATE
        // v4.2.2: Now passes intended direction for counter-trend filter
        // =================================================================
        const HFTProfile& hft = ResolveHFTProfile(sym);
        
        // Update displacement tracking
        st.updateDisplacement(ts);
        
        // Get intended direction BEFORE gate (for directional bias filter)
        int8_t intended_dir = st.trend();
        
        const char* block_reason = nullptr;
        if (!AllowTradeHFT(st, hft, ts, block_reason, sym.c_str(), intended_dir)) {
            sig.reason = block_reason;
            // v4.2.2: Map block reasons to metrics
            if (block_reason) {
                if (std::strcmp(block_reason, "LOW_EDGE") == 0 || 
                    std::strcmp(block_reason, "EDGE_LT_COST") == 0) {
                    Chimera::record_block(sym, Chimera::BlockReason::LOW_EDGE);
                } else if (std::strcmp(block_reason, "SPREAD_WIDE") == 0) {
                    Chimera::record_block(sym, Chimera::BlockReason::SPREAD_WIDE);
                } else if (std::strcmp(block_reason, "COOLDOWN") == 0) {
                    Chimera::record_block(sym, Chimera::BlockReason::COOLDOWN);
                } else {
                    Chimera::record_block(sym, Chimera::BlockReason::OTHER);
                }
            }
            return sig;
        }
        
        // v4.2.2: Record successful trade
        Chimera::record_block(sym, Chimera::BlockReason::NONE);

        // =================================================================
        // v4.2.2: DUAL-PATH ENTRY LOGIC (THE ARCHITECTURAL FIX)
        // 
        // OPEN session:   Burst-first mode - momentum direction, no trend required
        // ACTIVE session: Trend-first mode - trend + signal required
        // FADE session:   No new entries
        // =================================================================
        SessionState session = GetSessionState();
        int8_t dir = 0;
        
        switch (session) {
            case SessionState::OPEN:
                // BURST-FIRST MODE: Use momentum direction, not trend
                // At session open, trend signals are unreliable
                // Momentum direction captures the burst direction
                dir = (st.momentum > 0) ? 1 : (st.momentum < 0) ? -1 : 0;
                if (dir == 0) {
                    // Even momentum is flat - use micro-trend EMA as fallback
                    dir = (st.micro_trend_ema > 0.0001) ? 1 : 
                          (st.micro_trend_ema < -0.0001) ? -1 : 0;
                }
                if (dir == 0) {
                    sig.reason = "OPEN_NO_DIRECTION";
                    return sig;
                }
                // Log that we're using burst-first mode
                static thread_local uint64_t last_open_log = 0;
                if (ts - last_open_log > 5'000'000'000ULL) {  // Every 5s
                    std::cout << "[SESSION-OPEN] " << sym 
                              << " using momentum dir=" << (int)dir 
                              << " (burst-first mode)\n";
                    last_open_log = ts;
                }
                break;
                
            case SessionState::ACTIVE:
                // TREND-FIRST MODE: Require proper trend
                dir = st.trend();
                if (dir == 0) {
                    sig.reason = "NO_TREND";
                    return sig;
                }
                break;
                
            case SessionState::FADE:
                // NO NEW ENTRIES: Late session, avoid chop
                sig.reason = "SESSION_FADE";
                return sig;
        }
        
        // =================================================================
        // GATE 4: FLIP DIRECTION BLOCK (if flips disabled)
        // =================================================================
        if (!profile.allow_flip && st.last_trade_direction != 0) {
            if (dir == -st.last_trade_direction) {
                int64_t elapsed = now_ms - st.last_trade_ms;
                // Even stricter: 3x min_hold for flips when disabled
                if (elapsed < profile.min_hold_ms * 3) {
                    sig.reason = "FLIP_BLOCKED";
                    return sig;
                }
            }
        }

        // =================================================================
        // GATE 5: MICRO STATE GATE
        // =================================================================
        MicroDecision decision = micro.allowEntry(dir, sprdBps, hft.tp_bps);
        sig.micro_state = decision.current_state;
        sig.veto_reason = decision.veto;

        if (!decision.allow_trade) {
            sig.reason = vetoStr(decision.veto);
            return sig;
        }

        // =================================================================
        // GATE 6: CONFIDENCE CALCULATION
        // =================================================================
        double confidence = calculateConfidence(st, dir, sprdBps, profile);
        
        if (confidence < profile.min_conf_entry) {
            sig.reason = "LOW_CONF";
            return sig;
        }

        // =================================================================
        // EXECUTE ENTRY
        // =================================================================
        sig.direction = dir;
        sig.confidence = confidence;
        
        // v4.2.2: SESSION-AWARE RISK SCALING
        // OPEN: 70% size (cautious participation in auction)
        // ACTIVE: 100% size (full aggression)
        // FADE: 0% (shouldn't reach here due to earlier gate)
        double session_mult = SessionRiskMultiplier(session);
        sig.size = cfg_.size * session_mult;
        
        sig.reason = (dir > 0) ? "BUY" : "SELL";
        sig.spread_bps = sprdBps;

        st.pos.open(dir, st.mid, sig.size, ts, now_ms, sprdBps);
        st.last_trade_ms = now_ms;
        st.last_trade_ns = ts;  // v4.2: Track for frequency limit
        st.last_trade_direction = dir;

        micro.onEntry(dir, ts);

        // v4.2.2: Compute and save entry edge for exit logic
        double raw_edge = std::abs(st.momentum) / st.mid * 10000.0 * 10;
        double edge = std::min(raw_edge, st.realized_vol_bps * hft.vol_cap_mult);
        st.entry_edge_bps = edge;  // Save for edge-decay exit
        
        // v4.2.2: LOUD ENTRY LOGGING - ALWAYS SHOW
        std::cout << "\n"
                  << "▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶\n"
                  << "▶▶  ENTRY  " << symbol << "  " << (dir > 0 ? "LONG" : "SHORT") << "  @" << std::fixed << std::setprecision(5) << st.mid << "\n"
                  << "▶▶  edge=" << std::setprecision(1) << edge << "bps  spread=" << sprdBps << "bps  disp=" << st.getDisplacementBps() << "bps\n"
                  << "▶▶  session=" << SessionStateStr(session) << "  size=" << std::setprecision(4) << sig.size << " (×" << std::setprecision(1) << session_mult << ")\n"
                  << "▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶\n\n";
        std::cout.flush();

        return sig;
    }

    // =========================================================================
    // v4.2: Handle loss - activate cooldown
    // =========================================================================
    void onLoss(const std::string& symbol, uint64_t now_ns) {
        auto it = states_.find(symbol);
        if (it != states_.end()) {
            const HFTProfile& hft = ResolveHFTProfile(symbol);
            it->second.cooldown_until_ns = now_ns + hft.cooldown_after_loss_ns;
        }
    }

    // =========================================================================
    // CHECK EXIT - With HFT profile parameters (v4.2)
    // v4.2.2: WIN RATE IMPROVEMENTS - Asymmetric exits, time-based stop
    // =========================================================================
    ScalpSignal checkExit(SymbolState& st, uint64_t /*ts*/, MicroStateMachine& /*micro*/, 
                          int64_t now_ms, const SymbolProfile& profile, const HFTProfile& hft) {
        ScalpSignal sig;
        auto& pos = st.pos;
        if (!pos.active) return sig;

        double pnl_bps = pos.pnlBps(st.mid);
        int64_t hold_ms = now_ms - pos.entry_time_ms;
        bool canExit = hold_ms >= profile.min_hold_ms;
        
        // v4.2: Use HFT profile values for TP/SL
        double tp_bps = hft.tp_bps;
        
        // v4.2: Dynamic SL floor - never tighter than 2× spread + buffer
        double spread_bps = st.spreadBps();
        double min_sl = std::max(hft.min_sl_floor_bps, spread_bps * 2.0 + 1.0);
        double sl_bps = std::max(hft.sl_bps, min_sl);

        auto calcCurrencyPnL = [&]() -> double {
            double pnl_points = (st.mid - pos.entry_price) * pos.side;
            return pnl_points * pos.size * cfg_.contract_size;
        };

        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: WIN RATE IMPROVEMENT - ASYMMETRIC EXITS
        // Take profit quickly at 60% of target (locks small wins more often)
        // Win rate ↑, slightly lower avg win, keeps PF stable
        // ═══════════════════════════════════════════════════════════════════
        double fast_tp_bps = tp_bps * 0.6;  // 60% of full TP
        
        // Compute current edge for decay detection
        double current_edge_bps = std::abs(st.momentum) / st.mid * 10000.0 * 10;
        
        // v4.2.2: Edge-decay aware TP - take profit if price moved but edge collapsed
        if (canExit && pnl_bps >= fast_tp_bps && 
            current_edge_bps < st.entry_edge_bps * 0.4) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "TP_EDGE_DECAY";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }
        
        // Fast TP - Lock small wins quickly (legacy path)
        if (canExit && pnl_bps >= fast_tp_bps) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "TP_FAST";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: ADVERSE FLOW CUT - Exit early if flow flips against us
        // Cuts losers before SL when micro-edge turns negative
        // ═══════════════════════════════════════════════════════════════════
        if (hold_ms > 100 &&  // Give trade a chance
            current_edge_bps < 0.5 &&  // Edge collapsed
            pnl_bps < -spread_bps * 0.5 &&  // Already losing
            pnl_bps > -sl_bps * 0.6) {  // Not yet at full SL
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "ADVERSE_FLOW";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: WIN RATE IMPROVEMENT - TIME-BASED STOP (SLOW BLEED EXIT)
        // If trade hasn't moved in our favor within 800ms → exit as scratch
        // Make it volatility-aware
        // ═══════════════════════════════════════════════════════════════════
        static constexpr int64_t SLOW_BLEED_MS = 800;  // 800ms
        double slow_bleed_threshold = std::max(0.5, st.realized_vol_bps * 0.15);
        
        if (hold_ms > SLOW_BLEED_MS && pnl_bps < slow_bleed_threshold && pnl_bps > -sl_bps * 0.4) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "SLOW_BLEED";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // Full TP - Take Profit (keep for larger moves)
        if (canExit && pnl_bps >= tp_bps) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "TP";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // SL - Stop Loss (ALWAYS execute, ignore canExit)
        // v4.2: Using dynamic SL floor
        if (pnl_bps <= -sl_bps) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "SL";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // Trailing stop
        if (canExit && pnl_bps >= profile.trail_start_bps) {
            double peak = pos.side > 0 ? pos.highest : pos.lowest;
            double peakPnl = (peak - pos.entry_price) / pos.entry_price * 10000.0 * pos.side;
            if (peakPnl - pnl_bps > profile.trail_stop_bps) {
                sig.direction = -pos.side;
                sig.size = pos.size;
                sig.confidence = 1.0;
                sig.reason = "TRAIL";
                sig.is_exit = true;
                sig.realized_pnl_bps = pnl_bps;
                sig.realized_pnl = calcCurrencyPnL();
                sig.entry_price = pos.entry_price;
                sig.exit_price = st.mid;
                pos.close();
                return sig;
            }
        }

        // TIME EXIT - Max hold exceeded (use HFT profile)
        int64_t max_hold_ms = hft.max_hold_ns / 1'000'000;
        if (hold_ms >= max_hold_ms) {
            double exit_pnl_bps = pnl_bps;
            double exit_pnl = calcCurrencyPnL();
            
            if (cfg_.debug) {
                std::cout << "[SCALP] TIME_EXIT pnl_bps=" << exit_pnl_bps << "\n";
            }
            
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "TIME";
            sig.is_exit = true;
            sig.realized_pnl_bps = exit_pnl_bps;
            sig.realized_pnl = exit_pnl;
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        sig.reason = "HOLDING";
        return sig;
    }

    const SymbolState* getState(const char* s) const {
        auto it = states_.find(s);
        return it != states_.end() ? &it->second : nullptr;
    }
    
    // v4.2.2: Non-const version for updating latency
    SymbolState* getSymbolState(const char* s) {
        auto it = states_.find(s);
        return it != states_.end() ? &it->second : nullptr;
    }

    MicroStateManager& getMicroManager() { return microMgr_; }

    std::string getDiagnostics(const std::string& sym) const {
        return microMgr_.getDiagnostics(sym);
    }

    void reset() {
        states_.clear();
        microMgr_.reset();
    }
    
    // v7.11: Get expectancy for a symbol
    double getExpectancy(const std::string& sym) const {
        auto it = states_.find(sym);
        if (it != states_.end()) {
            return it->second.expectancy.getExpectancy();
        }
        return 0;
    }
    
    // v7.11: Check if symbol is disabled
    bool isDisabled(const std::string& sym) const {
        auto it = states_.find(sym);
        if (it != states_.end()) {
            return it->second.expectancy.disabled;
        }
        return false;
    }
    
    // v7.11: Reset expectancy for a symbol (re-enable)
    void resetExpectancy(const std::string& sym) {
        auto it = states_.find(sym);
        if (it != states_.end()) {
            it->second.expectancy.reset();
        }
    }
    
    // v4.2: Get block stats for a symbol (diagnostics)
    struct BlockStats {
        uint64_t cost = 0;
        uint64_t chop = 0;
        uint64_t vol = 0;
        uint64_t cooldown = 0;
        uint64_t frequency = 0;
        uint64_t total() const { return cost + chop + vol + cooldown + frequency; }
    };
    
    BlockStats getBlockStats(const std::string& sym) const {
        BlockStats stats;
        auto it = states_.find(sym);
        if (it != states_.end()) {
            const auto& st = it->second;
            stats.cost = st.blocked_cost;
            stats.chop = st.blocked_chop;
            stats.vol = st.blocked_vol;
            stats.cooldown = st.blocked_cooldown;
            stats.frequency = st.blocked_frequency;
        }
        return stats;
    }
    
    // v4.2: Print block stats for all symbols
    void printBlockStats() const {
        std::cout << "\n=== HFT BLOCK STATS ===\n";
        for (const auto& [sym, st] : states_) {
            uint64_t total = st.blocked_cost + st.blocked_chop + st.blocked_vol 
                           + st.blocked_cooldown + st.blocked_frequency;
            if (total == 0) continue;
            
            std::cout << sym << ": "
                      << "COST=" << st.blocked_cost 
                      << " CHOP=" << st.blocked_chop
                      << " VOL=" << st.blocked_vol
                      << " COOLDOWN=" << st.blocked_cooldown
                      << " FREQ=" << st.blocked_frequency
                      << " | total=" << total << "\n";
        }
        std::cout << "========================\n";
    }
    
    // v7.11: Get full stats for GUI - returns all active symbols' expectancy
    struct ExpectancyStats {
        std::string symbol;
        int trades;
        double expectancy_bps;
        double win_rate;
        double flip_rate;
        double avg_hold_ms;
        bool disabled;
        const char* disable_reason;
    };
    
    std::vector<ExpectancyStats> getAllExpectancyStats() const {
        std::vector<ExpectancyStats> stats;
        for (const auto& [sym, st] : states_) {
            const auto& e = st.expectancy;
            stats.push_back({
                sym,
                e.getTradeCount(),
                e.getExpectancy(),
                e.getWinRate(),
                e.getFlipRate(),
                e.getAvgHoldMs(),
                e.disabled,
                e.disable_reason
            });
        }
        return stats;
    }

private:
    Config cfg_;
    std::unordered_map<std::string, SymbolState> states_;
    MicroStateManager microMgr_;
    Chimera::EdgeController edge_controller_;  // v4.2.2: Dynamic edge weighting
    
    static int64_t getNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    // =========================================================================
    // SESSION WINDOW CHECK (UTC)
    // =========================================================================
    bool isInSessionWindow(const SymbolProfile& p) const {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm* utc = std::gmtime(&t);
        
        int current_mins = utc->tm_hour * 60 + utc->tm_min;
        int start_mins = p.session_start_hour * 60 + p.session_start_min;
        int end_mins = p.session_end_hour * 60 + p.session_end_min;
        
        return current_mins >= start_mins && current_mins < end_mins;
    }
    
    // =========================================================================
    // CONFIDENCE CALCULATION - Profile-aware
    // =========================================================================
    double calculateConfidence(const SymbolState& st, int8_t dir, 
                               double sprdBps, const SymbolProfile& profile) const {
        double conf = 0.5;
        
        // Trend alignment (+0.15)
        if (st.trend() == dir) {
            conf += 0.15;
        }
        
        // Momentum alignment (+0.10)
        if ((dir > 0 && st.momentum > 0) || (dir < 0 && st.momentum < 0)) {
            conf += 0.10;
        }
        
        // VWAP alignment (+0.10)
        double vwap_dev = (st.mid - st.vwap) / st.vwap;
        if ((dir > 0 && vwap_dev < -0.001) || (dir < 0 && vwap_dev > 0.001)) {
            conf += 0.10;
        }
        
        // Spread quality bonus
        if (sprdBps < profile.max_spread_bps * 0.5) {
            conf += 0.10;
        } else if (sprdBps < profile.max_spread_bps * 0.75) {
            conf += 0.05;
        }
        
        // v4.2: REMOVED double volatility penalty
        // Volatility already caps edge in AllowTradeHFT - second penalty is fatal
        
        return std::clamp(conf, 0.0, 1.0);
    }
};

} // namespace Omega
