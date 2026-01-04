// ═══════════════════════════════════════════════════════════════════════════════
// cfd_engine/include/config/BlackBullConfig.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// VERSION: v1.0.0
// OWNER: Jo
//
// PURPOSE:
// Unified configuration for BlackBull Markets CFD trading.
// Combines spread tables, news filter, and capital scaling.
//
// USAGE:
//   #include "config/BlackBullConfig.hpp"
//   using namespace chimera::cfd::config;
//
//   // Check if we can trade
//   auto gate = BlackBullGate::check("NAS100", current_spread, regime, dd_state);
//   if (gate.allowed) {
//       double size = gate.position_size;
//       // ... execute trade
//   }
//
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "BlackBullSpreadTables.hpp"
#include "NewsFilter.hpp"
#include "CapitalScaling.hpp"

#include <ctime>
#include <iostream>
#include <iomanip>

namespace chimera {
namespace cfd {
namespace config {

// ═══════════════════════════════════════════════════════════════════════════════
// UNIFIED GATE RESULT
// ═══════════════════════════════════════════════════════════════════════════════

struct BlackBullGateResult {
    // Overall decision
    bool allowed;
    const char* block_reason;
    
    // Component results
    SpreadGateResult spread_gate;
    NewsFilterResult news_gate;
    PositionSizeResult size_result;
    
    // Session info
    TradingSession session;
    double session_multiplier;
    
    // Final position sizing
    double position_size;
    double risk_amount;
    
    // Convenience
    explicit operator bool() const noexcept { return allowed; }
    
    void print() const {
        std::cout << "[BLACKBULL-GATE] " << (allowed ? "✓ ALLOWED" : "✗ BLOCKED");
        if (!allowed && block_reason) {
            std::cout << " - " << block_reason;
        }
        std::cout << "\n";
        
        std::cout << "  Session: " << session_str(session) 
                  << " (mult=" << std::fixed << std::setprecision(1) << session_multiplier << "x)\n";
        
        std::cout << "  Spread: " << (spread_gate.allowed ? "OK" : "BLOCKED");
        if (!spread_gate.allowed && spread_gate.block_reason) {
            std::cout << " (" << spread_gate.block_reason << ")";
        }
        std::cout << "\n";
        
        std::cout << "  News: " << (news_gate.blocked ? "BLOCKED" : "CLEAR");
        if (news_gate.blocked && news_gate.reason) {
            std::cout << " (" << news_gate.reason << ", " << news_gate.seconds_until_clear << "s)";
        }
        std::cout << "\n";
        
        std::cout << "  Size: " << std::setprecision(4) << position_size 
                  << " (risk $" << std::setprecision(2) << risk_amount << ")\n";
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// BLACKBULL GATE - UNIFIED ENTRY CHECK
// ═══════════════════════════════════════════════════════════════════════════════

class BlackBullGate {
public:
    /**
     * Comprehensive entry gate check for BlackBull CFD trading
     * 
     * Checks:
     * 1. Session & spread gate (from BlackBullSpreadTables)
     * 2. News filter (high-impact event blocking)
     * 3. Drawdown limits
     * 4. Position sizing with all multipliers
     * 
     * @param symbol Symbol to trade (e.g., "NAS100")
     * @param current_spread Current market spread
     * @param regime Current market regime
     * @param dd_state Current drawdown state
     * @param equity Account equity
     * @param stop_distance_pct Stop distance as % of entry
     * @param concurrent_positions Number of currently open positions
     * @param config Capital configuration (uses defaults if not specified)
     * @return BlackBullGateResult with decision and sizing
     */
    static BlackBullGateResult check(
        const char* symbol,
        double current_spread,
        MarketRegime regime,
        const DrawdownState& dd_state,
        double equity,
        double stop_distance_pct,
        int concurrent_positions = 0,
        const CapitalConfig& config = CapitalConfig::defaults()
    ) noexcept {
        BlackBullGateResult result;
        result.allowed = false;
        result.block_reason = nullptr;
        result.position_size = 0.0;
        result.risk_amount = 0.0;
        
        // Get current time
        time_t now = time(nullptr);
        struct tm* utc = gmtime(&now);
        if (!utc) {
            result.block_reason = "TIME_ERROR";
            return result;
        }
        
        // 1. Check spread gate
        result.spread_gate = check_spread_gate(
            symbol, current_spread, utc->tm_hour, utc->tm_min
        );
        result.session = result.spread_gate.session;
        result.session_multiplier = get_session_size_multiplier(result.session);
        
        if (!result.spread_gate.allowed) {
            result.block_reason = result.spread_gate.block_reason;
            return result;
        }
        
        // 2. Check news filter
        result.news_gate = get_news_calendar().check(
            symbol, static_cast<uint64_t>(now)
        );
        
        if (result.news_gate.blocked) {
            result.block_reason = result.news_gate.reason;
            return result;
        }
        
        // 3. Check regime compatibility
        // TRANSITION regime gets reduced size handled by spread gate mult
        // VOLATILE regime may need special handling
        if (regime == MarketRegime::UNKNOWN) {
            result.block_reason = "REGIME_UNKNOWN";
            return result;
        }
        
        // 4. Calculate position size
        double spread_mult = result.spread_gate.size_multiplier;
        
        result.size_result = calculate_position_size(
            equity,
            stop_distance_pct,
            result.session_multiplier,
            spread_mult,
            dd_state,
            config,
            concurrent_positions
        );
        
        if (!result.size_result.allowed) {
            result.block_reason = result.size_result.block_reason;
            return result;
        }
        
        // All checks passed
        result.allowed = true;
        result.position_size = result.size_result.size;
        result.risk_amount = result.size_result.risk_amount;
        
        return result;
    }
    
    /**
     * Quick check - just spread + news, no sizing
     */
    static bool quick_check(
        const char* symbol,
        double current_spread
    ) noexcept {
        // Get current time
        time_t now = time(nullptr);
        struct tm* utc = gmtime(&now);
        if (!utc) return false;
        
        // Check spread
        auto spread_result = check_spread_gate(
            symbol, current_spread, utc->tm_hour, utc->tm_min
        );
        if (!spread_result.allowed) return false;
        
        // Check news
        auto news_result = get_news_calendar().check(
            symbol, static_cast<uint64_t>(now)
        );
        if (news_result.blocked) return false;
        
        return true;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SCALE-UP CHECK (UNIFIED)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Check if scale-up is allowed for an existing position
 */
inline ScaleUpGateResult check_scale_up_now(
    const char* symbol,
    double current_spread,
    double current_pnl_r,
    MarketRegime regime,
    int adds_so_far,
    double initial_size,
    double entry_price,
    double current_price,
    bool is_long,
    const CapitalConfig& config = CapitalConfig::defaults()
) noexcept {
    // First check spread gate
    auto spread_gate = check_spread_gate_now(symbol, current_spread);
    if (!spread_gate.allowed) {
        ScaleUpGateResult result;
        result.allowed = false;
        result.block_reason = spread_gate.block_reason;
        return result;
    }
    
    // Check news
    if (is_news_blocked(symbol)) {
        ScaleUpGateResult result;
        result.allowed = false;
        result.block_reason = "NEWS_BLOCKED";
        return result;
    }
    
    // Get session multiplier
    TradingSession session = get_current_session_now();
    double session_mult = get_session_size_multiplier(session);
    
    // Check scale-up conditions
    return check_scale_up(
        config,
        current_pnl_r,
        session_mult,
        regime,
        adds_so_far,
        initial_size,
        entry_price,
        current_price,
        is_long
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// DAILY STATUS PRINTER
// ═══════════════════════════════════════════════════════════════════════════════

inline void print_trading_status(
    const DrawdownState& dd_state,
    const CapitalConfig& config
) {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║               BLACKBULL TRADING STATUS                           ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
    
    // Current session
    TradingSession session = get_current_session_now();
    double session_mult = get_session_size_multiplier(session);
    std::cout << "║  Session:  " << std::setw(15) << std::left << session_str(session)
              << " (multiplier: " << std::fixed << std::setprecision(1) << session_mult << "x)         ║\n";
    
    // Drawdown status
    std::cout << "╠──────────────────────────────────────────────────────────────────╣\n";
    std::cout << "║  Daily PnL:    " << std::setw(8) << std::right << std::setprecision(2) 
              << dd_state.daily_pnl_r << "R"
              << "  (max DD: " << std::setprecision(1) << dd_state.daily_max_dd_r << "R / " 
              << config.daily_max_dd_r << "R)       ║\n";
    std::cout << "║  Weekly PnL:   " << std::setw(8) << std::right << std::setprecision(2) 
              << dd_state.weekly_pnl_r << "R"
              << "  (max DD: " << std::setprecision(1) << dd_state.weekly_max_dd_r << "R / " 
              << config.weekly_max_dd_r << "R)       ║\n";
    std::cout << "║  Session PnL:  " << std::setw(8) << std::right << std::setprecision(2) 
              << dd_state.session_pnl_r << "R"
              << "  (max DD: " << std::setprecision(1) << dd_state.session_max_dd_r << "R / " 
              << config.session_max_dd_r << "R)       ║\n";
    
    // Flags
    std::cout << "╠──────────────────────────────────────────────────────────────────╣\n";
    std::cout << "║  Daily stopped:     " << (dd_state.daily_stopped ? "YES ✗" : "NO  ✓") << "                                    ║\n";
    std::cout << "║  Session stopped:   " << (dd_state.session_stopped ? "YES ✗" : "NO  ✓") << "                                    ║\n";
    std::cout << "║  Size reduced:      " << (dd_state.weekly_size_reduced ? "YES (50%)" : "NO  (100%)") << "                              ║\n";
    
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
}

} // namespace config
} // namespace cfd
} // namespace chimera
