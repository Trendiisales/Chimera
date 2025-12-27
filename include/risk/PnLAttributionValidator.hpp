// =============================================================================
// PnLAttributionValidator.hpp - Production PnL Attribution & Auto-Disable
// =============================================================================
// PURPOSE: Track realized PnL, spread costs, and auto-disable negative expectancy
//
// FEATURES:
//   - Per-symbol realized PnL tracking (only on close)
//   - Spread cost attribution
//   - Commission tracking
//   - Win rate calculation
//   - Expectancy computation
//   - Auto-disable symbols with negative expectancy
//   - Cooldown-based re-testing
//   - CSV export for analysis
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <cstdio>

namespace Chimera {

struct TradeRecord {
    double entry_price = 0.0;
    double exit_price  = 0.0;
    double size        = 0.0;
    double spread_bps  = 0.0;
    double commission  = 0.0;
    int    side        = 0; // +1 buy, -1 sell
};

struct SymbolStats {
    uint64_t trades = 0;
    uint64_t wins   = 0;
    uint64_t losses = 0;

    double gross_pnl   = 0.0;
    double spread_cost = 0.0;
    double commissions = 0.0;

    uint64_t disabled_at_ms = 0;
    bool disabled = false;

    double expectancy() const {
        if (trades == 0) return 0.0;
        return (gross_pnl - spread_cost - commissions) / static_cast<double>(trades);
    }

    double win_rate() const {
        if (trades == 0) return 0.0;
        return static_cast<double>(wins) / static_cast<double>(trades);
    }
    
    double net_pnl() const {
        return gross_pnl - spread_cost - commissions;
    }
};

class PnLAttributionValidator {
public:
    explicit PnLAttributionValidator(const std::string& csv_path = "")
        : csv_path_(csv_path)
    {
        if (!csv_path_.empty()) {
            csv_.open(csv_path_, std::ios::out | std::ios::app);
            if (csv_.tellp() == 0) {
                csv_ << "timestamp_ms,symbol,trades,win_rate,expectancy,gross_pnl,spread_cost,fees,net_pnl,disabled\n";
            }
        }
    }

    ~PnLAttributionValidator() {
        if (csv_.is_open()) {
            csv_.close();
        }
    }

    // =========================================================================
    // TRADE CLOSE HANDLER (only call on position close)
    // =========================================================================
    void onTradeClose(
        const std::string& symbol,
        const TradeRecord& t,
        uint64_t now_ms
    ) {
        auto& s = stats_[symbol];
        s.trades++;

        // PnL calculation: (exit - entry) * side * size
        double pnl = (t.exit_price - t.entry_price) * t.side * t.size;

        s.gross_pnl   += pnl;
        s.spread_cost += t.spread_bps * t.size * 0.0001 * t.entry_price;  // Convert bps to currency
        s.commissions += t.commission;

        if (pnl > 0) s.wins++;
        else s.losses++;

        evaluate(symbol, now_ms);
        
        if (csv_.is_open()) {
            exportCSV(symbol, now_ms);
        }
        
        // Log trade close
        printf("[PNL-CLOSE] %s side=%d pnl=%.4f gross=%.4f spread=%.4f net=%.4f trades=%lu\n",
               symbol.c_str(), t.side, pnl, s.gross_pnl, s.spread_cost, s.net_pnl(), s.trades);
    }

    // =========================================================================
    // TRADABILITY CHECK (call before any new trade)
    // =========================================================================
    bool isTradable(const std::string& symbol, uint64_t now_ms) {
        auto it = stats_.find(symbol);
        if (it == stats_.end()) return true; // No data yet, allow

        auto& s = it->second;

        if (!s.disabled) return true;

        // Cooldown elapsed → allow re-test
        if (now_ms - s.disabled_at_ms > cooldown_ms_) {
            printf("[PNL-RETEST] %s re-enabled after %.1fs cooldown\n", 
                   symbol.c_str(), cooldown_ms_ / 1000.0);
            s.disabled = false;
            resetStats(s);
            return true;
        }

        return false;
    }

    // =========================================================================
    // PERIODIC REPORTING
    // =========================================================================
    void periodicReport(uint64_t now_ms) {
        if (now_ms - last_report_ms_ < report_interval_ms_) return;
        last_report_ms_ = now_ms;

        if (stats_.empty()) return;

        printf("\n[PNL-REPORT] ========================================\n");
        for (const auto& kv : stats_) {
            const auto& sym = kv.first;
            const auto& s   = kv.second;

            printf("[PNL-ATTR] %s trades=%lu win%%=%.1f exp=%.5f gross=%.2f spread=%.2f fees=%.2f net=%.2f %s\n",
                sym.c_str(),
                s.trades,
                s.win_rate() * 100.0,
                s.expectancy(),
                s.gross_pnl,
                s.spread_cost,
                s.commissions,
                s.net_pnl(),
                s.disabled ? "DISABLED" : "ACTIVE"
            );
        }
        printf("[PNL-REPORT] ========================================\n\n");
    }

    // =========================================================================
    // ACCESSORS
    // =========================================================================
    const SymbolStats* getStats(const std::string& symbol) const {
        auto it = stats_.find(symbol);
        return (it != stats_.end()) ? &it->second : nullptr;
    }

    double totalNetPnL() const {
        double total = 0.0;
        for (const auto& kv : stats_) {
            total += kv.second.net_pnl();
        }
        return total;
    }

    uint64_t totalTrades() const {
        uint64_t total = 0;
        for (const auto& kv : stats_) {
            total += kv.second.trades;
        }
        return total;
    }

    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    void setMinTrades(uint64_t n) { min_trades_ = n; }
    void setMinExpectancy(double e) { min_expectancy_ = e; }
    void setCooldownMs(uint64_t ms) { cooldown_ms_ = ms; }
    void setReportIntervalMs(uint64_t ms) { report_interval_ms_ = ms; }

private:
    void evaluate(const std::string& symbol, uint64_t now_ms) {
        auto& s = stats_[symbol];

        if (s.trades < min_trades_) return;

        if (!s.disabled && s.expectancy() < min_expectancy_) {
            s.disabled = true;
            s.disabled_at_ms = now_ms;
            printf("[PNL-BLOCK] %s DISABLED (exp=%.5f < %.5f after %lu trades)\n", 
                   symbol.c_str(), s.expectancy(), min_expectancy_, s.trades);
        }
    }

    void resetStats(SymbolStats& s) {
        s.trades = s.wins = s.losses = 0;
        s.gross_pnl = s.spread_cost = s.commissions = 0.0;
    }

    void exportCSV(const std::string& symbol, uint64_t now_ms) {
        const auto& s = stats_[symbol];
        csv_
            << now_ms << ","
            << symbol << ","
            << s.trades << ","
            << s.win_rate() << ","
            << s.expectancy() << ","
            << s.gross_pnl << ","
            << s.spread_cost << ","
            << s.commissions << ","
            << s.net_pnl() << ","
            << (s.disabled ? 1 : 0)
            << "\n";
        csv_.flush();
    }

private:
    std::unordered_map<std::string, SymbolStats> stats_;

    std::ofstream csv_;
    std::string csv_path_;

    uint64_t last_report_ms_ = 0;

    // Configuration (sensible defaults)
    uint64_t min_trades_        = 30;       // Need 30 trades for statistical meaning
    double   min_expectancy_    = 0.0;      // Must be >= 0 to continue
    uint64_t cooldown_ms_       = 300000;   // 5 min cooldown before re-test
    uint64_t report_interval_ms_ = 30000;   // Report every 30s
};

} // namespace Chimera
