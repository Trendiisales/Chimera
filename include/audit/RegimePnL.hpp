// ═══════════════════════════════════════════════════════════════════════════════
// include/audit/RegimePnL.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: REGIME × ALPHA × HOUR PnL ATTRIBUTION
//
// PURPOSE: Answer objectively:
// - Which regimes make money?
// - Which alphas work inside those regimes?
// - Which combinations are decaying?
//
// Institutions do NOT trust blended PnL.
// They attribute every dollar to its source.
//
// OUTPUT: CSV file that is the SINGLE SOURCE OF TRUTH
// ML, GUI, and humans read this file — not each other.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "../alpha/MarketRegime.hpp"
#include "../alpha/AlphaSelector.hpp"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <map>
#include <fstream>
#include <mutex>

namespace Chimera {
namespace Audit {

// ─────────────────────────────────────────────────────────────────────────────
// Attribution Key - Immutable context for each trade
// ─────────────────────────────────────────────────────────────────────────────
struct AttributionKey {
    Alpha::MarketRegime regime = Alpha::MarketRegime::DEAD;
    Alpha::AlphaType alpha = Alpha::AlphaType::NONE;
    uint8_t hour = 0;                  // UTC hour (0-23)
    char symbol[16] = {0};
    
    bool operator<(const AttributionKey& other) const {
        if (regime != other.regime) return regime < other.regime;
        if (alpha != other.alpha) return alpha < other.alpha;
        if (hour != other.hour) return hour < other.hour;
        return strcmp(symbol, other.symbol) < 0;
    }
    
    bool operator==(const AttributionKey& other) const {
        return regime == other.regime && 
               alpha == other.alpha && 
               hour == other.hour &&
               strcmp(symbol, other.symbol) == 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Trade Attribution - Raw truth for each fill
// ─────────────────────────────────────────────────────────────────────────────
struct TradeAttribution {
    AttributionKey key;
    
    // PnL components (in quote currency, e.g., USD)
    double gross_pnl = 0.0;
    double fees = 0.0;
    double slippage = 0.0;
    double net_pnl = 0.0;              // gross - fees - slippage
    
    // Trade outcome
    bool win = false;
    double hold_time_sec = 0.0;
    
    // Timestamp
    uint64_t entry_ts = 0;
    uint64_t exit_ts = 0;
    
    // Additional context
    double entry_price = 0.0;
    double exit_price = 0.0;
    double size = 0.0;
    int8_t side = 0;                   // 1 = long, -1 = short
};

// ─────────────────────────────────────────────────────────────────────────────
// Regime Stats - Aggregated statistics
// ─────────────────────────────────────────────────────────────────────────────
struct RegimeStats {
    int trades = 0;
    int wins = 0;
    double gross_pnl = 0.0;
    double net_pnl = 0.0;
    double total_fees = 0.0;
    double total_slippage = 0.0;
    double total_hold_sec = 0.0;
    
    // Profit factor tracking (SPEC FIX: no placeholders)
    double total_win_pnl = 0.0;   // Sum of all winning trade P&L
    double total_loss_pnl = 0.0;  // Sum of all losing trade P&L (absolute value)
    
    // Derived metrics
    double winRate() const {
        return trades > 0 ? static_cast<double>(wins) / trades : 0.0;
    }
    
    double avgNetPnl() const {
        return trades > 0 ? net_pnl / trades : 0.0;
    }
    
    double avgHoldSec() const {
        return trades > 0 ? total_hold_sec / trades : 0.0;
    }
    
    double profitFactor() const {
        // SPEC FIX: Real profit factor = gross wins / gross losses
        if (total_loss_pnl <= 0.0) {
            return total_win_pnl > 0.0 ? 999.0 : 0.0;  // No losses = infinite (capped)
        }
        return total_win_pnl / total_loss_pnl;
    }
    
    double expectancy() const {
        // Expected value per trade
        if (trades <= 0) return 0.0;
        double wr = winRate();
        double avg_win = wins > 0 ? total_win_pnl / wins : 0.0;
        double losses = trades - wins;
        double avg_loss = losses > 0 ? total_loss_pnl / losses : 0.0;
        return (wr * avg_win) - ((1.0 - wr) * avg_loss);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Attribution Table - Aggregated by key
// ─────────────────────────────────────────────────────────────────────────────
using RegimePnLTable = std::map<AttributionKey, RegimeStats>;

// ─────────────────────────────────────────────────────────────────────────────
// Regime PnL Tracker - Main class
// ─────────────────────────────────────────────────────────────────────────────
class RegimePnLTracker {
public:
    RegimePnLTracker(const char* csv_path = "runtime/audit/regime_pnl.csv")
        : csv_path_(csv_path) {}
    
    // Record a completed trade
    void recordTrade(const TradeAttribution& attr) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Update aggregated stats
        auto& s = table_[attr.key];
        s.trades++;
        if (attr.win) {
            s.wins++;
            s.total_win_pnl += attr.net_pnl;  // SPEC FIX: Track wins
        } else {
            s.total_loss_pnl += std::abs(attr.net_pnl);  // SPEC FIX: Track losses (abs)
        }
        s.gross_pnl += attr.gross_pnl;
        s.net_pnl += attr.net_pnl;
        s.total_fees += attr.fees;
        s.total_slippage += attr.slippage;
        s.total_hold_sec += attr.hold_time_sec;
        
        // Append to raw log
        raw_log_.push_back(attr);
        
        // Persist periodically
        if (++trades_since_persist_ >= PERSIST_INTERVAL) {
            persistCSV();
            trades_since_persist_ = 0;
        }
    }
    
    // Get stats for a specific combination
    RegimeStats getStats(Alpha::MarketRegime regime, Alpha::AlphaType alpha) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        RegimeStats combined;
        for (const auto& [key, stats] : table_) {
            if (key.regime == regime && key.alpha == alpha) {
                combined.trades += stats.trades;
                combined.wins += stats.wins;
                combined.gross_pnl += stats.gross_pnl;
                combined.net_pnl += stats.net_pnl;
                combined.total_fees += stats.total_fees;
                combined.total_slippage += stats.total_slippage;
                combined.total_hold_sec += stats.total_hold_sec;
            }
        }
        return combined;
    }
    
    // Get stats by regime only
    RegimeStats getRegimeStats(Alpha::MarketRegime regime) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        RegimeStats combined;
        for (const auto& [key, stats] : table_) {
            if (key.regime == regime) {
                combined.trades += stats.trades;
                combined.wins += stats.wins;
                combined.net_pnl += stats.net_pnl;
            }
        }
        return combined;
    }
    
    // Get stats by alpha only
    RegimeStats getAlphaStats(Alpha::AlphaType alpha) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        RegimeStats combined;
        for (const auto& [key, stats] : table_) {
            if (key.alpha == alpha) {
                combined.trades += stats.trades;
                combined.wins += stats.wins;
                combined.net_pnl += stats.net_pnl;
            }
        }
        return combined;
    }
    
    // Get stats by hour
    RegimeStats getHourStats(int hour) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        RegimeStats combined;
        for (const auto& [key, stats] : table_) {
            if (key.hour == hour) {
                combined.trades += stats.trades;
                combined.wins += stats.wins;
                combined.net_pnl += stats.net_pnl;
            }
        }
        return combined;
    }
    
    // Persist to CSV
    void persistCSV() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ofstream file(csv_path_);
        if (!file.is_open()) return;
        
        // Header
        file << "SYMBOL,REGIME,ALPHA,HOUR,TRADES,WINS,WINRATE,GROSS_PNL,NET_PNL,FEES,SLIPPAGE,AVG_HOLD_SEC\n";
        
        // Data
        for (const auto& [key, s] : table_) {
            file << key.symbol << ","
                 << Alpha::regimeStr(key.regime) << ","
                 << Alpha::alphaTypeStr(key.alpha) << ","
                 << static_cast<int>(key.hour) << ","
                 << s.trades << ","
                 << s.wins << ","
                 << std::fixed << std::setprecision(3) << s.winRate() << ","
                 << std::setprecision(2) << s.gross_pnl << ","
                 << s.net_pnl << ","
                 << s.total_fees << ","
                 << s.total_slippage << ","
                 << std::setprecision(1) << s.avgHoldSec() << "\n";
        }
        
        file.close();
    }
    
    // Print summary to console
    void printSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n══════════════════════════════════════════════════════════════\n");
        printf("  REGIME × ALPHA PnL ATTRIBUTION\n");
        printf("══════════════════════════════════════════════════════════════\n");
        
        // By regime
        printf("\nBy Regime:\n");
        for (int r = 0; r < 4; r++) {
            auto regime = static_cast<Alpha::MarketRegime>(r);
            double net = 0.0;
            int trades = 0;
            for (const auto& [key, s] : table_) {
                if (key.regime == regime) {
                    net += s.net_pnl;
                    trades += s.trades;
                }
            }
            if (trades > 0) {
                printf("  %-12s: %4d trades, $%+.2f net\n",
                       Alpha::regimeStr(regime), trades, net);
            }
        }
        
        // By alpha
        printf("\nBy Alpha:\n");
        for (int a = 1; a < 4; a++) {  // Skip NONE
            auto alpha = static_cast<Alpha::AlphaType>(a);
            double net = 0.0;
            int trades = 0;
            int wins = 0;
            for (const auto& [key, s] : table_) {
                if (key.alpha == alpha) {
                    net += s.net_pnl;
                    trades += s.trades;
                    wins += s.wins;
                }
            }
            if (trades > 0) {
                double wr = static_cast<double>(wins) / trades * 100.0;
                printf("  %-14s: %4d trades, %.1f%% WR, $%+.2f net\n",
                       Alpha::alphaTypeStr(alpha), trades, wr, net);
            }
        }
        
        printf("══════════════════════════════════════════════════════════════\n\n");
    }
    
    // Reset (for new day)
    void resetDaily() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Archive current data before reset
        persistCSV();
        table_.clear();
        trades_since_persist_ = 0;
    }
    
    size_t totalTrades() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& [key, s] : table_) {
            total += s.trades;
        }
        return total;
    }
    
    const RegimePnLTable& table() const { return table_; }
    
private:
    static constexpr int PERSIST_INTERVAL = 10;  // Trades between persists
    
    mutable std::mutex mutex_;
    RegimePnLTable table_;
    std::vector<TradeAttribution> raw_log_;
    std::string csv_path_;
    int trades_since_persist_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Tracker
// ─────────────────────────────────────────────────────────────────────────────
inline RegimePnLTracker& getRegimePnLTracker() {
    static RegimePnLTracker tracker;
    return tracker;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Create attribution from trade context
// ─────────────────────────────────────────────────────────────────────────────
inline TradeAttribution createAttribution(
    const char* symbol,
    Alpha::MarketRegime regime,
    Alpha::AlphaType alpha,
    int utc_hour,
    double entry_price,
    double exit_price,
    double size,
    int8_t side,
    double fees,
    uint64_t entry_ts,
    uint64_t exit_ts
) {
    TradeAttribution attr;
    
    // Key
    attr.key.regime = regime;
    attr.key.alpha = alpha;
    attr.key.hour = static_cast<uint8_t>(utc_hour);
    strncpy(attr.key.symbol, symbol, 15);
    
    // Calculate PnL
    double price_diff = (exit_price - entry_price) * side;
    attr.gross_pnl = price_diff * size;
    attr.fees = fees;
    attr.slippage = 0.0;  // Would need more data for accurate slippage
    attr.net_pnl = attr.gross_pnl - attr.fees - attr.slippage;
    
    // Outcome
    attr.win = attr.net_pnl > 0;
    attr.hold_time_sec = static_cast<double>(exit_ts - entry_ts) / 1'000'000'000.0;
    
    // Context
    attr.entry_price = entry_price;
    attr.exit_price = exit_price;
    attr.size = size;
    attr.side = side;
    attr.entry_ts = entry_ts;
    attr.exit_ts = exit_ts;
    
    return attr;
}

} // namespace Audit
} // namespace Chimera
