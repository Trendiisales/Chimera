// =============================================================================
// BacktestHarness_v4.10.2.hpp - VALIDATION BACKTEST (NOT OPTIMIZATION)
// =============================================================================
// PURPOSE: Prove exits + E2 dominance behave as intended
//
// WHAT WE'RE TESTING:
//   ✅ Trades/day ≈ 0-2
//   ✅ Partial exits actually fire
//   ✅ Stall kills happen
//   ✅ Few full SLs
//   ✅ No weird clustering
//   ✅ Daily loss halt exists but stays quiet
//
// WHAT WE'RE NOT DOING:
//   ❌ Multi-year optimizations
//   ❌ Walk-forward tuning
//   ❌ Monte Carlo
//   ❌ Parameter sweeps
//   ❌ Adding symbols
//   ❌ Adding logic
//
// YOU STOP BACKTESTING WHEN:
//   - You can't learn anything new without real fills
//   - Every loss is explainable
//   - No trade surprises you
//   - You feel bored, not curious
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>

#include "engines/IndexImpulseEngine.hpp"
#include "portfolio/PortfolioModeController.hpp"
#include "integration/CfdEngineIntegration.hpp"

namespace Chimera {

// =============================================================================
// TRADE RECORD FOR ANALYSIS
// =============================================================================
struct BacktestTrade {
    std::string symbol;
    std::string date;
    int8_t direction = 0;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double size = 0.0;
    double pnl_r = 0.0;
    double pnl_dollars = 0.0;
    
    // Exit classification
    enum class ExitType {
        STOP_LOSS,
        PARTIAL,
        STALL_KILL,
        TRAILING,
        EOD
    };
    ExitType exit_type = ExitType::STOP_LOSS;
    
    uint32_t bars_held = 0;
    std::string engine = "E2";
    std::string entry_reason;
    std::string exit_reason;
};

inline const char* exitTypeStr(BacktestTrade::ExitType t) {
    switch (t) {
        case BacktestTrade::ExitType::STOP_LOSS:  return "STOP_LOSS";
        case BacktestTrade::ExitType::PARTIAL:    return "PARTIAL";
        case BacktestTrade::ExitType::STALL_KILL: return "STALL_KILL";
        case BacktestTrade::ExitType::TRAILING:   return "TRAILING";
        case BacktestTrade::ExitType::EOD:        return "EOD";
        default:                                   return "UNKNOWN";
    }
}

// =============================================================================
// DAILY STATS
// =============================================================================
struct DailyStats {
    std::string date;
    int trades_nas100 = 0;
    int trades_us30 = 0;
    double pnl_dollars = 0.0;
    double pnl_r = 0.0;
    double max_drawdown_r = 0.0;
    bool daily_halt_triggered = false;
    
    int partials = 0;
    int stall_kills = 0;
    int stop_losses = 0;
    int trailing_exits = 0;
};

// =============================================================================
// BACKTEST REPORT
// =============================================================================
struct BacktestReport {
    // Summary
    int total_days = 0;
    int trading_days = 0;
    int total_trades = 0;
    double total_pnl_r = 0.0;
    double total_pnl_dollars = 0.0;
    
    // Trade frequency
    double avg_trades_per_day = 0.0;
    int max_trades_per_day = 0;
    int days_with_zero_trades = 0;
    
    // Exit distribution (THIS IS WHAT WE CARE ABOUT)
    int exit_stop_loss = 0;
    int exit_partial = 0;
    int exit_stall_kill = 0;
    int exit_trailing = 0;
    int exit_eod = 0;
    
    // R-distribution
    double avg_win_r = 0.0;
    double avg_loss_r = 0.0;
    double win_rate = 0.0;
    double profit_factor = 0.0;
    double expectancy_r = 0.0;
    
    // Risk metrics
    double max_drawdown_r = 0.0;
    double max_consecutive_losses = 0;
    int daily_halt_count = 0;
    
    // Bars held distribution
    double avg_bars_held_winners = 0.0;
    double avg_bars_held_losers = 0.0;
    
    // Per-symbol breakdown
    struct SymbolStats {
        std::string symbol;
        int trades = 0;
        double pnl_r = 0.0;
        double win_rate = 0.0;
        int partials = 0;
        int stall_kills = 0;
    };
    std::vector<SymbolStats> symbol_stats;
    
    // Raw data
    std::vector<BacktestTrade> trades;
    std::vector<DailyStats> daily_stats;
    
    void print() const {
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("BACKTEST REPORT - v4.10.2 VALIDATION\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        
        printf("\n📊 SUMMARY\n");
        printf("  Days: %d (trading: %d)\n", total_days, trading_days);
        printf("  Trades: %d\n", total_trades);
        printf("  P&L: %.2fR ($%.2f)\n", total_pnl_r, total_pnl_dollars);
        
        printf("\n📈 TRADE FREQUENCY (target: 0-2 per day)\n");
        printf("  Avg trades/day: %.2f %s\n", avg_trades_per_day,
               avg_trades_per_day <= 2.0 ? "✅" : "⚠️");
        printf("  Max trades/day: %d %s\n", max_trades_per_day,
               max_trades_per_day <= 4 ? "✅" : "⚠️");
        printf("  Days with 0 trades: %d (%.1f%%)\n", 
               days_with_zero_trades, 
               100.0 * days_with_zero_trades / std::max(1, trading_days));
        
        printf("\n🚪 EXIT DISTRIBUTION (THIS IS THE KEY CHECK)\n");
        int total_exits = exit_stop_loss + exit_partial + exit_stall_kill + 
                         exit_trailing + exit_eod;
        printf("  PARTIAL:    %3d (%5.1f%%) %s\n", exit_partial,
               100.0 * exit_partial / std::max(1, total_exits),
               exit_partial > 0 ? "✅ Partials fire" : "❌ NO PARTIALS");
        printf("  STALL_KILL: %3d (%5.1f%%) %s\n", exit_stall_kill,
               100.0 * exit_stall_kill / std::max(1, total_exits),
               exit_stall_kill > 0 ? "✅ Stalls fire" : "⚠️ Check stall logic");
        printf("  TRAILING:   %3d (%5.1f%%)\n", exit_trailing,
               100.0 * exit_trailing / std::max(1, total_exits));
        printf("  STOP_LOSS:  %3d (%5.1f%%) %s\n", exit_stop_loss,
               100.0 * exit_stop_loss / std::max(1, total_exits),
               exit_stop_loss < total_exits * 0.5 ? "✅ Not all SLs" : "⚠️ Too many SLs");
        printf("  EOD:        %3d (%5.1f%%)\n", exit_eod,
               100.0 * exit_eod / std::max(1, total_exits));
        
        printf("\n📉 PERFORMANCE\n");
        printf("  Win rate: %.1f%%\n", win_rate * 100.0);
        printf("  Avg win:  %.2fR\n", avg_win_r);
        printf("  Avg loss: %.2fR\n", avg_loss_r);
        printf("  Expectancy: %.3fR\n", expectancy_r);
        printf("  Profit factor: %.2f\n", profit_factor);
        
        printf("\n⚠️ RISK/DAMAGE CONTROL\n");
        printf("  Max drawdown: %.2fR %s\n", max_drawdown_r,
               max_drawdown_r < 5.0 ? "✅" : "⚠️");
        printf("  Max consecutive losses: %.0f\n", max_consecutive_losses);
        printf("  Daily halts triggered: %d %s\n", daily_halt_count,
               daily_halt_count == 0 ? "✅ Quiet" : "⚠️ Investigate");
        
        printf("\n⏱️ BARS HELD\n");
        printf("  Winners avg: %.1f bars\n", avg_bars_held_winners);
        printf("  Losers avg:  %.1f bars %s\n", avg_bars_held_losers,
               avg_bars_held_losers < 8 ? "✅ Stalls working" : "⚠️ Holding losers too long");
        
        printf("\n📋 PER-SYMBOL\n");
        for (const auto& ss : symbol_stats) {
            printf("  %s: %d trades, %.2fR, %.1f%% win, %d partials, %d stalls\n",
                   ss.symbol.c_str(), ss.trades, ss.pnl_r, 
                   ss.win_rate * 100.0, ss.partials, ss.stall_kills);
        }
        
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        
        // PASS/FAIL summary
        printf("\n🎯 VALIDATION CHECKLIST:\n");
        printf("  [%s] Trades/day ≈ 0-2\n", avg_trades_per_day <= 2.5 ? "✅" : "❌");
        printf("  [%s] Partial exits fire\n", exit_partial > 0 ? "✅" : "❌");
        printf("  [%s] Stall kills happen\n", exit_stall_kill > 0 ? "✅" : "❌");
        printf("  [%s] Not all exits are SL\n", exit_stop_loss < total_exits * 0.6 ? "✅" : "❌");
        printf("  [%s] Daily halt stays quiet\n", daily_halt_count == 0 ? "✅" : "⚠️");
        printf("  [%s] Max DD < 5R\n", max_drawdown_r < 5.0 ? "✅" : "❌");
        
        int pass_count = 0;
        if (avg_trades_per_day <= 2.5) pass_count++;
        if (exit_partial > 0) pass_count++;
        if (exit_stall_kill > 0) pass_count++;
        if (exit_stop_loss < total_exits * 0.6) pass_count++;
        if (daily_halt_count == 0) pass_count++;
        if (max_drawdown_r < 5.0) pass_count++;
        
        printf("\n  RESULT: %d/6 checks passed\n", pass_count);
        if (pass_count >= 5) {
            printf("  → READY FOR MICRO-LIVE\n");
        } else {
            printf("  → FIX BEFORE LIVE\n");
        }
        printf("\n");
    }
    
    void exportCSV(const std::string& filename) const {
        std::ofstream f(filename);
        f << "date,symbol,direction,entry,exit,size,pnl_r,pnl_usd,exit_type,bars,engine,entry_reason,exit_reason\n";
        for (const auto& t : trades) {
            f << t.date << "," 
              << t.symbol << ","
              << (t.direction > 0 ? "LONG" : "SHORT") << ","
              << std::fixed << std::setprecision(2) << t.entry_price << ","
              << t.exit_price << ","
              << std::setprecision(4) << t.size << ","
              << std::setprecision(2) << t.pnl_r << ","
              << t.pnl_dollars << ","
              << exitTypeStr(t.exit_type) << ","
              << t.bars_held << ","
              << t.engine << ","
              << t.entry_reason << ","
              << t.exit_reason << "\n";
        }
        f.close();
        printf("[BACKTEST] Exported %zu trades to %s\n", trades.size(), filename.c_str());
    }
};

// =============================================================================
// BAR DATA
// =============================================================================
struct Bar {
    std::string date;
    std::string time;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    uint64_t timestamp_ns = 0;
};

// =============================================================================
// BACKTEST HARNESS
// =============================================================================
class BacktestHarness {
public:
    BacktestHarness() = default;
    
    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    void setEquity(double equity) { starting_equity_ = equity; }
    void setVerbose(bool v) { verbose_ = v; }
    
    // =========================================================================
    // LOAD BAR DATA FROM CSV
    // Expected format: date,time,open,high,low,close,volume
    // =========================================================================
    bool loadBars(const std::string& symbol, const std::string& filename) {
        std::ifstream f(filename);
        if (!f.is_open()) {
            printf("[BACKTEST] ERROR: Cannot open %s\n", filename.c_str());
            return false;
        }
        
        std::vector<Bar> bars;
        std::string line;
        std::getline(f, line);  // Skip header
        
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string token;
            Bar bar;
            
            std::getline(ss, bar.date, ',');
            std::getline(ss, bar.time, ',');
            std::getline(ss, token, ','); bar.open = std::stod(token);
            std::getline(ss, token, ','); bar.high = std::stod(token);
            std::getline(ss, token, ','); bar.low = std::stod(token);
            std::getline(ss, token, ','); bar.close = std::stod(token);
            std::getline(ss, token, ','); bar.volume = std::stod(token);
            
            bars.push_back(bar);
        }
        
        symbol_bars_[symbol] = bars;
        printf("[BACKTEST] Loaded %zu bars for %s\n", bars.size(), symbol.c_str());
        return true;
    }
    
    // =========================================================================
    // RUN BACKTEST
    // =========================================================================
    BacktestReport run() {
        BacktestReport report;
        
        // Initialize engine
        auto& integration = getCfdEngineIntegration();
        integration.init(starting_equity_);
        
        printf("\n[BACKTEST] Starting v4.10.2 validation backtest\n");
        printf("[BACKTEST] Equity: $%.2f\n", starting_equity_);
        printf("[BACKTEST] Symbols: NAS100, US30 only\n");
        printf("[BACKTEST] Risk: NAS100=0.50%%, US30=0.40%% (FIXED)\n");
        
        // Merge bars from all symbols into timeline
        std::vector<std::pair<std::string, Bar>> timeline;
        for (const auto& [symbol, bars] : symbol_bars_) {
            for (const auto& bar : bars) {
                timeline.push_back({symbol, bar});
            }
        }
        
        // Sort by date+time
        std::sort(timeline.begin(), timeline.end(), 
            [](const auto& a, const auto& b) {
                return a.second.date + a.second.time < b.second.date + b.second.time;
            });
        
        printf("[BACKTEST] Processing %zu bars\n", timeline.size());
        
        // Track current day
        std::string current_date;
        DailyStats daily;
        double equity = starting_equity_;
        double peak_equity = equity;
        double running_dd = 0.0;
        int consecutive_losses = 0;
        int max_consecutive_losses = 0;
        
        // Process each bar
        for (size_t i = 0; i < timeline.size(); i++) {
            const auto& [symbol, bar] = timeline[i];
            
            // New day?
            if (bar.date != current_date) {
                if (!current_date.empty()) {
                    // Save previous day
                    report.daily_stats.push_back(daily);
                    
                    // Reset for new day
                    integration.resetDaily(equity);
                }
                
                current_date = bar.date;
                daily = DailyStats{};
                daily.date = current_date;
                report.total_days++;
            }
            
            // Simulate tick at bar close (simplified)
            double mid = bar.close;
            double spread = mid * 0.0001;  // 1 pip spread assumption
            double bid = mid - spread/2;
            double ask = mid + spread/2;
            
            auto result = integration.onTick(symbol.c_str(), bid, ask, bar.volume, 0);
            
            if (result.should_trade) {
                BacktestTrade trade;
                trade.symbol = symbol;
                trade.date = bar.date;
                trade.direction = result.direction;
                trade.size = result.size;
                trade.engine = result.engine;
                trade.entry_reason = result.reason;
                
                if (result.is_exit) {
                    // This is an exit
                    trade.exit_price = mid;
                    trade.exit_reason = result.reason;
                    trade.pnl_dollars = result.realized_pnl;
                    
                    // Classify exit type
                    if (strcmp(result.reason, "STOP_LOSS") == 0) {
                        trade.exit_type = BacktestTrade::ExitType::STOP_LOSS;
                        report.exit_stop_loss++;
                        daily.stop_losses++;
                    } else if (strcmp(result.reason, "PARTIAL_PROFIT") == 0 || result.is_partial) {
                        trade.exit_type = BacktestTrade::ExitType::PARTIAL;
                        report.exit_partial++;
                        daily.partials++;
                    } else if (strcmp(result.reason, "STALL_KILL") == 0) {
                        trade.exit_type = BacktestTrade::ExitType::STALL_KILL;
                        report.exit_stall_kill++;
                        daily.stall_kills++;
                    } else if (strstr(result.reason, "TRAIL") != nullptr) {
                        trade.exit_type = BacktestTrade::ExitType::TRAILING;
                        report.exit_trailing++;
                        daily.trailing_exits++;
                    }
                    
                    // Update equity
                    equity += trade.pnl_dollars;
                    daily.pnl_dollars += trade.pnl_dollars;
                    
                    // Track consecutive losses
                    if (trade.pnl_dollars < 0) {
                        consecutive_losses++;
                        max_consecutive_losses = std::max(max_consecutive_losses, consecutive_losses);
                    } else {
                        consecutive_losses = 0;
                    }
                    
                    // Track drawdown
                    peak_equity = std::max(peak_equity, equity);
                    double dd = (peak_equity - equity) / starting_equity_;
                    running_dd = std::max(running_dd, dd);
                    
                    if (verbose_) {
                        printf("[BACKTEST] %s %s EXIT %s @ %.2f PnL=$%.2f (%s)\n",
                               bar.date.c_str(), symbol.c_str(),
                               result.direction > 0 ? "SHORT" : "LONG",
                               mid, trade.pnl_dollars,
                               exitTypeStr(trade.exit_type));
                    }
                    
                    report.trades.push_back(trade);
                    report.total_trades++;
                    
                } else {
                    // This is an entry
                    trade.entry_price = mid;
                    
                    if (symbol == "NAS100") daily.trades_nas100++;
                    if (symbol == "US30") daily.trades_us30++;
                    
                    if (verbose_) {
                        printf("[BACKTEST] %s %s ENTRY %s @ %.2f\n",
                               bar.date.c_str(), symbol.c_str(),
                               result.direction > 0 ? "LONG" : "SHORT", mid);
                    }
                }
            }
            
            // Bar close event
            integration.onBarClose(symbol.c_str());
        }
        
        // Save last day
        if (!current_date.empty()) {
            report.daily_stats.push_back(daily);
        }
        
        // Calculate statistics
        calculateStats(report);
        report.max_drawdown_r = running_dd / 0.005;  // Convert to R (assuming 0.5% risk)
        report.max_consecutive_losses = max_consecutive_losses;
        
        return report;
    }
    
private:
    double starting_equity_ = 100000.0;
    bool verbose_ = false;
    std::unordered_map<std::string, std::vector<Bar>> symbol_bars_;
    
    void calculateStats(BacktestReport& report) {
        if (report.daily_stats.empty()) return;
        
        // Trading days (days with at least one trade)
        report.trading_days = 0;
        report.days_with_zero_trades = 0;
        report.max_trades_per_day = 0;
        int total_daily_trades = 0;
        
        for (const auto& day : report.daily_stats) {
            int day_trades = day.trades_nas100 + day.trades_us30;
            if (day_trades > 0) {
                report.trading_days++;
                total_daily_trades += day_trades;
                report.max_trades_per_day = std::max(report.max_trades_per_day, day_trades);
            } else {
                report.days_with_zero_trades++;
            }
            
            if (day.daily_halt_triggered) {
                report.daily_halt_count++;
            }
        }
        
        if (report.trading_days > 0) {
            report.avg_trades_per_day = (double)total_daily_trades / report.trading_days;
        }
        
        // Win/loss stats
        int wins = 0, losses = 0;
        double total_win_r = 0.0, total_loss_r = 0.0;
        double total_bars_winners = 0.0, total_bars_losers = 0.0;
        
        for (const auto& t : report.trades) {
            if (t.pnl_dollars >= 0) {
                wins++;
                total_win_r += t.pnl_r;
                total_bars_winners += t.bars_held;
            } else {
                losses++;
                total_loss_r += std::abs(t.pnl_r);
                total_bars_losers += t.bars_held;
            }
            
            report.total_pnl_r += t.pnl_r;
            report.total_pnl_dollars += t.pnl_dollars;
        }
        
        if (wins > 0) report.avg_win_r = total_win_r / wins;
        if (losses > 0) report.avg_loss_r = total_loss_r / losses;
        if (wins + losses > 0) report.win_rate = (double)wins / (wins + losses);
        if (total_loss_r > 0) report.profit_factor = total_win_r / total_loss_r;
        
        report.expectancy_r = report.win_rate * report.avg_win_r - 
                             (1.0 - report.win_rate) * report.avg_loss_r;
        
        if (wins > 0) report.avg_bars_held_winners = total_bars_winners / wins;
        if (losses > 0) report.avg_bars_held_losers = total_bars_losers / losses;
        
        // Per-symbol stats
        std::unordered_map<std::string, BacktestReport::SymbolStats> sym_map;
        for (const auto& t : report.trades) {
            auto& ss = sym_map[t.symbol];
            ss.symbol = t.symbol;
            ss.trades++;
            ss.pnl_r += t.pnl_r;
            if (t.pnl_dollars >= 0) ss.win_rate += 1.0;
            if (t.exit_type == BacktestTrade::ExitType::PARTIAL) ss.partials++;
            if (t.exit_type == BacktestTrade::ExitType::STALL_KILL) ss.stall_kills++;
        }
        
        for (auto& [sym, ss] : sym_map) {
            if (ss.trades > 0) ss.win_rate /= ss.trades;
            report.symbol_stats.push_back(ss);
        }
    }
};

// =============================================================================
// SENSITIVITY TEST (OPTIONAL - NOT OPTIMIZATION)
// =============================================================================
struct SensitivityResult {
    std::string param_name;
    double param_value;
    double expectancy_r;
    double max_dd_r;
    int exit_partials;
    int exit_stalls;
    bool stable;  // Performance didn't collapse
};

class SensitivityTest {
public:
    // Test partial size: 50% vs 60%
    static std::vector<SensitivityResult> testPartialSize(
        BacktestHarness& harness,
        const std::vector<double>& values = {0.50, 0.60}
    ) {
        std::vector<SensitivityResult> results;
        
        printf("\n[SENSITIVITY] Testing partial size: ");
        for (double v : values) printf("%.0f%% ", v * 100);
        printf("\n");
        
        for (double partial_pct : values) {
            // Modify config
            IndexEngineConfig cfg;
            cfg.partial_pct = partial_pct;
            getIndexImpulseEngine().setConfig(cfg);
            
            // Run backtest
            auto report = harness.run();
            
            SensitivityResult r;
            r.param_name = "partial_pct";
            r.param_value = partial_pct;
            r.expectancy_r = report.expectancy_r;
            r.max_dd_r = report.max_drawdown_r;
            r.exit_partials = report.exit_partial;
            r.exit_stalls = report.exit_stall_kill;
            r.stable = (report.expectancy_r > -0.1 && report.max_drawdown_r < 10.0);
            
            results.push_back(r);
        }
        
        // Reset to default
        IndexEngineConfig cfg;
        cfg.partial_pct = 0.60;
        getIndexImpulseEngine().setConfig(cfg);
        
        printSensitivityResults(results);
        return results;
    }
    
    // Test stall bars: 5 vs 6 vs 7
    static std::vector<SensitivityResult> testStallBars(
        BacktestHarness& harness,
        const std::vector<int>& values = {5, 6, 7}
    ) {
        std::vector<SensitivityResult> results;
        
        printf("\n[SENSITIVITY] Testing stall bars: ");
        for (int v : values) printf("%d ", v);
        printf("\n");
        
        for (int stall_bars : values) {
            // Modify config
            IndexEngineConfig cfg;
            cfg.stall_bars = stall_bars;
            getIndexImpulseEngine().setConfig(cfg);
            
            // Run backtest
            auto report = harness.run();
            
            SensitivityResult r;
            r.param_name = "stall_bars";
            r.param_value = stall_bars;
            r.expectancy_r = report.expectancy_r;
            r.max_dd_r = report.max_drawdown_r;
            r.exit_partials = report.exit_partial;
            r.exit_stalls = report.exit_stall_kill;
            r.stable = (report.expectancy_r > -0.1 && report.max_drawdown_r < 10.0);
            
            results.push_back(r);
        }
        
        // Reset to default
        IndexEngineConfig cfg;
        cfg.stall_bars = 6;
        getIndexImpulseEngine().setConfig(cfg);
        
        printSensitivityResults(results);
        return results;
    }
    
private:
    static void printSensitivityResults(const std::vector<SensitivityResult>& results) {
        printf("\n[SENSITIVITY] Results:\n");
        printf("  %-15s %10s %10s %10s %10s %8s\n", 
               "Param", "Value", "Expect", "MaxDD", "Partials", "Stable");
        printf("  %-15s %10s %10s %10s %10s %8s\n",
               "-----", "-----", "------", "-----", "--------", "------");
        
        bool all_stable = true;
        for (const auto& r : results) {
            printf("  %-15s %10.2f %10.3fR %10.2fR %10d %8s\n",
                   r.param_name.c_str(), r.param_value,
                   r.expectancy_r, r.max_dd_r, r.exit_partials,
                   r.stable ? "✅" : "❌");
            if (!r.stable) all_stable = false;
        }
        
        printf("\n");
        if (all_stable) {
            printf("  [SENSITIVITY] ✅ System is stable across parameter variations\n");
        } else {
            printf("  [SENSITIVITY] ⚠️ System is FRAGILE - some parameters break it\n");
            printf("  [SENSITIVITY] → DO NOT GO LIVE UNTIL FIXED\n");
        }
        printf("\n");
    }
};

} // namespace Chimera
