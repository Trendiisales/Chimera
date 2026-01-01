// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/ExpectancyTracker.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Track per-symbol expectancy and auto-disable negative expectancy
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.13: NEW FILE - Core profitability enforcement
//
// PRINCIPLE: "A system that cannot lose large amounts is already profitable"
// - Track rolling expectancy per symbol
// - Auto-disable symbols with negative expectancy
// - Paper-trade auto-reenable after proving positive expectancy
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <iostream>

namespace Chimera {
namespace Crypto {

// ─────────────────────────────────────────────────────────────────────────────
// Trade Mode (per symbol)
// ─────────────────────────────────────────────────────────────────────────────
enum class SymbolTradeMode : uint8_t {
    LIVE     = 0,   // Real orders sent
    PAPER    = 1,   // Simulated fills only
    DISABLED = 2    // No trading at all
};

inline const char* mode_str(SymbolTradeMode m) noexcept {
    switch (m) {
        case SymbolTradeMode::LIVE:     return "LIVE";
        case SymbolTradeMode::PAPER:    return "PAPER";
        case SymbolTradeMode::DISABLED: return "DISABLED";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Regime Classification
// ─────────────────────────────────────────────────────────────────────────────
enum class MarketRegime : uint8_t {
    STABLE     = 0,   // Tradable
    TRANSITION = 1,   // Cooldown
    TOXIC      = 2    // No trade
};

inline const char* regime_str(MarketRegime r) noexcept {
    switch (r) {
        case MarketRegime::STABLE:     return "STABLE";
        case MarketRegime::TRANSITION: return "TRANSITION";
        case MarketRegime::TOXIC:      return "TOXIC";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Expectancy Tracker
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolExpectancy {
    // Stats
    int      trades      = 0;
    int      wins        = 0;
    int      losses      = 0;
    double   avg_win_bps = 0.0;
    double   avg_loss_bps = 0.0;
    double   total_pnl_bps = 0.0;
    
    // State
    SymbolTradeMode mode = SymbolTradeMode::LIVE;
    MarketRegime    regime = MarketRegime::STABLE;
    char            disable_reason[64] = "";
    uint64_t        last_disable_ts = 0;
    
    // Paper recovery tracking
    int      paper_trades = 0;
    double   paper_expectancy = 0.0;
    
    // Config
    static constexpr int    MIN_TRADES_FOR_EVAL = 50;
    static constexpr double DISABLE_THRESHOLD_BPS = -0.1;  // Disable if E < -0.1 bps
    static constexpr double REENABLE_THRESHOLD_BPS = 0.2;  // Reenable if paper E > +0.2 bps
    static constexpr int    PAPER_TRADES_FOR_REENABLE = 30;
    
    // Record a trade result
    void record(double pnl_bps) {
        trades++;
        total_pnl_bps += pnl_bps;
        
        if (pnl_bps > 0) {
            wins++;
            // Incremental average update
            avg_win_bps += (pnl_bps - avg_win_bps) / wins;
        } else if (pnl_bps < 0) {
            losses++;
            avg_loss_bps += (std::abs(pnl_bps) - avg_loss_bps) / losses;
        }
        // pnl_bps == 0 is a scratch, counted as trade but neither win nor loss
    }
    
    // Record paper trade (for disabled symbols)
    void record_paper(double pnl_bps) {
        paper_trades++;
        double alpha = 0.1;  // EWMA smoothing
        paper_expectancy = alpha * pnl_bps + (1.0 - alpha) * paper_expectancy;
    }
    
    // Calculate rolling expectancy (bps)
    [[nodiscard]] double expectancy_bps() const noexcept {
        if (trades < 20) return 0.0;  // Not enough data
        
        double win_rate = (double)wins / trades;
        double loss_rate = (double)losses / trades;
        
        return (avg_win_bps * win_rate) - (avg_loss_bps * loss_rate);
    }
    
    // Check if should auto-disable
    [[nodiscard]] bool should_disable() const noexcept {
        return trades >= MIN_TRADES_FOR_EVAL && 
               expectancy_bps() <= DISABLE_THRESHOLD_BPS;
    }
    
    // Check if should auto-reenable from paper
    [[nodiscard]] bool should_reenable() const noexcept {
        return mode == SymbolTradeMode::DISABLED &&
               paper_trades >= PAPER_TRADES_FOR_REENABLE &&
               paper_expectancy >= REENABLE_THRESHOLD_BPS;
    }
    
    // Win rate (0-100%)
    [[nodiscard]] double win_rate_pct() const noexcept {
        return trades > 0 ? (100.0 * wins / trades) : 0.0;
    }
    
    // Reset stats (careful!)
    void reset() {
        trades = wins = losses = 0;
        avg_win_bps = avg_loss_bps = total_pnl_bps = 0.0;
        paper_trades = 0;
        paper_expectancy = 0.0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Regime Classifier
// ─────────────────────────────────────────────────────────────────────────────
class RegimeClassifier {
public:
    struct Config {
        double max_spread_bps;
        double spread_std_threshold;
        double book_flip_threshold;
        double latency_jitter_threshold_ms;
        uint64_t transition_cooldown_ms;
        
        Config()
            : max_spread_bps(3.0)
            , spread_std_threshold(1.5)
            , book_flip_threshold(0.3)
            , latency_jitter_threshold_ms(5.0)
            , transition_cooldown_ms(2000)
        {}
    };
    
    explicit RegimeClassifier(const Config& cfg = Config()) : cfg_(cfg) {}
    
    // Update regime based on current market conditions
    MarketRegime classify(double spread_bps, double spread_std, 
                          double book_flip_rate, double latency_jitter_ms) noexcept {
        // TOXIC conditions (any one triggers)
        if (spread_bps > cfg_.max_spread_bps) {
            last_toxic_ts_ = current_ts_;
            return MarketRegime::TOXIC;
        }
        if (spread_std > cfg_.spread_std_threshold) {
            last_toxic_ts_ = current_ts_;
            return MarketRegime::TOXIC;
        }
        if (book_flip_rate > cfg_.book_flip_threshold) {
            last_toxic_ts_ = current_ts_;
            return MarketRegime::TOXIC;
        }
        if (latency_jitter_ms > cfg_.latency_jitter_threshold_ms) {
            last_toxic_ts_ = current_ts_;
            return MarketRegime::TOXIC;
        }
        
        // TRANSITION: recovering from TOXIC
        if (current_ts_ - last_toxic_ts_ < cfg_.transition_cooldown_ms * 1000000ULL) {
            return MarketRegime::TRANSITION;
        }
        
        return MarketRegime::STABLE;
    }
    
    void set_timestamp(uint64_t ts_ns) noexcept { current_ts_ = ts_ns; }
    
private:
    Config cfg_;
    uint64_t current_ts_ = 0;
    uint64_t last_toxic_ts_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ExpectancyTracker - Manages all symbols
// ─────────────────────────────────────────────────────────────────────────────
class ExpectancyTracker {
public:
    // Record a closed trade
    void record_trade(const std::string& symbol, double pnl_bps) {
        auto& exp = symbols_[symbol];
        
        if (exp.mode == SymbolTradeMode::DISABLED) {
            // Track paper performance for potential re-enable
            exp.record_paper(pnl_bps);
            check_reenable(symbol, exp);
        } else {
            exp.record(pnl_bps);
            check_disable(symbol, exp);
        }
    }
    
    // Get expectancy for symbol
    [[nodiscard]] const SymbolExpectancy& get(const std::string& symbol) const {
        static SymbolExpectancy empty;
        auto it = symbols_.find(symbol);
        return it != symbols_.end() ? it->second : empty;
    }
    
    // Get mutable reference
    [[nodiscard]] SymbolExpectancy& get_mut(const std::string& symbol) {
        return symbols_[symbol];
    }
    
    // Check if symbol is allowed to trade
    [[nodiscard]] bool can_trade(const std::string& symbol) const {
        auto it = symbols_.find(symbol);
        if (it == symbols_.end()) return true;  // New symbol, allow
        return it->second.mode != SymbolTradeMode::DISABLED;
    }
    
    // Get trade mode
    [[nodiscard]] SymbolTradeMode get_mode(const std::string& symbol) const {
        auto it = symbols_.find(symbol);
        if (it == symbols_.end()) return SymbolTradeMode::LIVE;
        return it->second.mode;
    }
    
    // Manual disable
    void disable_symbol(const std::string& symbol, const char* reason) {
        auto& exp = symbols_[symbol];
        exp.mode = SymbolTradeMode::DISABLED;
        snprintf(exp.disable_reason, sizeof(exp.disable_reason), "%s", reason);
        exp.last_disable_ts = get_timestamp_ms();
        exp.paper_trades = 0;
        exp.paper_expectancy = 0.0;
        
        std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║  🔴 AUTO-DISABLE: " << symbol << "\n";
        std::cout << "║  Reason: " << reason << "\n";
        std::cout << "║  Expectancy: " << exp.expectancy_bps() << " bps\n";
        std::cout << "║  Trades: " << exp.trades << " (W:" << exp.wins << " L:" << exp.losses << ")\n";
        std::cout << "║  Mode: PAPER_ONLY until proven\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    }
    
    // Enable symbol
    void enable_symbol(const std::string& symbol) {
        auto& exp = symbols_[symbol];
        exp.mode = SymbolTradeMode::LIVE;
        exp.disable_reason[0] = '\0';
        
        std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║  🟢 RE-ENABLE: " << symbol << "\n";
        std::cout << "║  Paper Expectancy: " << exp.paper_expectancy << " bps\n";
        std::cout << "║  Paper Trades: " << exp.paper_trades << "\n";
        std::cout << "║  Mode: LIVE\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    }
    
    // Summary for GUI
    void print_summary() const {
        std::cout << "\n=== EXPECTANCY SUMMARY ===\n";
        for (const auto& [sym, exp] : symbols_) {
            std::cout << sym << ": E=" << exp.expectancy_bps() << "bps"
                      << " W=" << exp.win_rate_pct() << "%"
                      << " T=" << exp.trades
                      << " Mode=" << mode_str(exp.mode) << "\n";
        }
        std::cout << "==========================\n\n";
    }

private:
    void check_disable(const std::string& symbol, SymbolExpectancy& exp) {
        if (exp.should_disable()) {
            char reason[64];
            snprintf(reason, sizeof(reason), "E=%.2fbps < %.2fbps threshold",
                     exp.expectancy_bps(), SymbolExpectancy::DISABLE_THRESHOLD_BPS);
            disable_symbol(symbol, reason);
        }
    }
    
    void check_reenable(const std::string& symbol, SymbolExpectancy& exp) {
        if (exp.should_reenable()) {
            enable_symbol(symbol);
            // Reset live stats after re-enable
            exp.reset();
        }
    }
    
    static uint64_t get_timestamp_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    std::unordered_map<std::string, SymbolExpectancy> symbols_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Crypto HFT Parameters (LIVE-READY, SURVIVABLE)
// ─────────────────────────────────────────────────────────────────────────────
namespace CryptoHFTParams {

// BTCUSDT - Baseline crypto scalping
struct BTCUSDT {
    static constexpr double min_imbalance = 0.18;
    static constexpr double min_imbalance_ms = 80;
    static constexpr double max_spread_bps = 2.5;
    static constexpr double entry_edge_bps = 1.2;
    static constexpr double take_profit_bps = 1.0;
    static constexpr double stop_loss_bps = 1.4;
    static constexpr uint64_t hold_ms = 1500;
    static constexpr uint64_t cooldown_ms = 300;
    static constexpr int min_book_levels = 10;
    static constexpr int max_trades_per_hour = 120;
};

// ETHUSDT - Slightly wider
struct ETHUSDT {
    static constexpr double min_imbalance = 0.20;
    static constexpr double min_imbalance_ms = 90;
    static constexpr double max_spread_bps = 3.0;
    static constexpr double entry_edge_bps = 1.4;
    static constexpr double take_profit_bps = 1.2;
    static constexpr double stop_loss_bps = 1.6;
    static constexpr uint64_t hold_ms = 1800;
    static constexpr uint64_t cooldown_ms = 350;
    static constexpr int min_book_levels = 10;
    static constexpr int max_trades_per_hour = 100;
};

// SOLUSDT - More volatile
struct SOLUSDT {
    static constexpr double min_imbalance = 0.25;
    static constexpr double min_imbalance_ms = 100;
    static constexpr double max_spread_bps = 4.0;
    static constexpr double entry_edge_bps = 1.8;
    static constexpr double take_profit_bps = 1.5;
    static constexpr double stop_loss_bps = 2.0;
    static constexpr uint64_t hold_ms = 1200;
    static constexpr uint64_t cooldown_ms = 400;
    static constexpr int min_book_levels = 10;
    static constexpr int max_trades_per_hour = 80;
};

} // namespace CryptoHFTParams

// ─────────────────────────────────────────────────────────────────────────────
// Size Multipliers
// ─────────────────────────────────────────────────────────────────────────────
namespace SizeFactors {

// Latency-based sizing (protect edge when slow)
inline double latency_factor(double latency_ms) noexcept {
    if (latency_ms <= 1.0) return 1.0;
    if (latency_ms <= 3.0) return 0.6;
    if (latency_ms <= 5.0) return 0.3;
    return 0.0;  // Hard block
}

// Expectancy-based sizing
inline double expectancy_factor(double expectancy_bps) noexcept {
    if (expectancy_bps < 0.0)  return 0.0;   // DISABLED
    if (expectancy_bps < 0.2)  return 0.5;   // Cautious
    if (expectancy_bps < 0.4)  return 1.0;   // Normal
    return 1.5;  // Scaled
}

// Regime-based sizing
inline double regime_factor(MarketRegime regime) noexcept {
    switch (regime) {
        case MarketRegime::STABLE:     return 1.0;
        case MarketRegime::TRANSITION: return 0.3;
        case MarketRegime::TOXIC:      return 0.0;
        default: return 0.0;
    }
}

// Session-based sizing (UTC hours)
inline double session_factor(int hour_utc) noexcept {
    // Asia liquidity burst
    if (hour_utc >= 0 && hour_utc < 2) return 0.9;
    // London open
    if (hour_utc >= 7 && hour_utc < 9) return 1.0;
    // US equities overlap
    if (hour_utc >= 13 && hour_utc < 15) return 1.2;
    // Dead hours
    if (hour_utc >= 21 && hour_utc < 24) return 0.5;
    return 0.8;
}

// Combined sizing
inline double combined_size_multiplier(
    double latency_ms,
    double expectancy_bps,
    MarketRegime regime,
    int hour_utc
) noexcept {
    double mult = latency_factor(latency_ms)
                * expectancy_factor(expectancy_bps)
                * regime_factor(regime)
                * session_factor(hour_utc);
    return std::max(0.0, std::min(2.0, mult));  // Clamp [0, 2]
}

} // namespace SizeFactors

} // namespace Crypto
} // namespace Chimera
