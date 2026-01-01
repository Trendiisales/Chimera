// =============================================================================
// PredatorExpectancy.hpp - v4.8.0 - PREDATOR EXPECTANCY TRACKER
// =============================================================================
// PURPOSE: Per-symbol rolling expectancy dashboard
//
// You do NOT judge by PnL alone.
// We log exactly the 5 metrics that matter — per symbol.
//
// METRICS TRACKED (per symbol):
//   - Avg loss size
//   - Avg win size
//   - Win/Loss ratio
//   - Avg time in losing trades
//   - Veto / exit reasons
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstdio>

namespace Chimera {

// =============================================================================
// PER-SYMBOL EXPECTANCY STATS
// =============================================================================
struct PredatorExpectancyStats {
    int wins = 0;
    int losses = 0;
    int scratches = 0;
    
    double winSum = 0.0;       // Sum of winning PnL (bps)
    double lossSum = 0.0;      // Sum of losing PnL (bps, stored as positive)
    
    uint64_t winTimeNs = 0;    // Total time in winning trades
    uint64_t lossTimeNs = 0;   // Total time in losing trades
    
    // Computed metrics
    double avgWin() const {
        return (wins > 0) ? winSum / static_cast<double>(wins) : 0.0;
    }
    
    double avgLoss() const {
        return (losses > 0) ? lossSum / static_cast<double>(losses) : 0.0;
    }
    
    double payoffRatio() const {
        double al = avgLoss();
        return (al > 0) ? avgWin() / al : 0.0;
    }
    
    double winRate() const {
        int total = wins + losses;
        return (total > 0) ? static_cast<double>(wins) / static_cast<double>(total) : 0.0;
    }
    
    double avgWinTimeMs() const {
        return (wins > 0) ? static_cast<double>(winTimeNs) / static_cast<double>(wins) / 1e6 : 0.0;
    }
    
    double avgLossTimeMs() const {
        return (losses > 0) ? static_cast<double>(lossTimeNs) / static_cast<double>(losses) / 1e6 : 0.0;
    }
    
    // Expectancy = (WinRate × AvgWin) - (LossRate × AvgLoss)
    double expectancy() const {
        int total = wins + losses;
        if (total == 0) return 0.0;
        double wr = winRate();
        return (wr * avgWin()) - ((1.0 - wr) * avgLoss());
    }
    
    // Health check
    bool isHealthy() const {
        if (wins + losses < 5) return true;  // Not enough data
        return payoffRatio() >= 1.5 && avgLoss() <= 0.30;  // bps
    }
    
    void print(const std::string& symbol) const {
        const char* status = isHealthy() ? "HEALTHY" : "⚠️ DEGRADED";
        
        printf("\n[PREDATOR][%s]\n", symbol.c_str());
        printf("  Trades:       %d W / %d L / %d S\n", wins, losses, scratches);
        printf("  Avg Win:      %.2f bps\n", avgWin());
        printf("  Avg Loss:     %.2f bps\n", avgLoss());
        printf("  Win/Loss:     %.2fx\n", payoffRatio());
        printf("  Win Rate:     %.1f%%\n", winRate() * 100.0);
        printf("  Avg Win Time: %.0f ms\n", avgWinTimeMs());
        printf("  Avg Loss Time:%.0f ms\n", avgLossTimeMs());
        printf("  Expectancy:   %.3f bps/trade\n", expectancy());
        printf("  Status:       %s\n", status);
    }
    
    void toJSON(char* buf, size_t buf_size, const std::string& symbol) const {
        snprintf(buf, buf_size,
            "{"
            "\"symbol\":\"%s\","
            "\"wins\":%d,"
            "\"losses\":%d,"
            "\"scratches\":%d,"
            "\"avg_win_bps\":%.4f,"
            "\"avg_loss_bps\":%.4f,"
            "\"payoff_ratio\":%.4f,"
            "\"win_rate\":%.4f,"
            "\"avg_win_time_ms\":%.2f,"
            "\"avg_loss_time_ms\":%.2f,"
            "\"expectancy_bps\":%.4f,"
            "\"healthy\":%s"
            "}",
            symbol.c_str(),
            wins, losses, scratches,
            avgWin(), avgLoss(), payoffRatio(), winRate(),
            avgWinTimeMs(), avgLossTimeMs(), expectancy(),
            isHealthy() ? "true" : "false"
        );
    }
};

// =============================================================================
// PREDATOR EXPECTANCY TRACKER
// =============================================================================
class PredatorExpectancy {
public:
    // =========================================================================
    // RECORD TRADE
    // =========================================================================
    void recordTrade(
        const std::string& sym,
        double pnl_bps,
        uint64_t heldNs
    ) {
        auto& s = stats_[sym];
        
        if (pnl_bps > 0.05) {  // Win (more than 0.05 bps)
            s.wins++;
            s.winSum += pnl_bps;
            s.winTimeNs += heldNs;
        } else if (pnl_bps < -0.05) {  // Loss (less than -0.05 bps)
            s.losses++;
            s.lossSum += (-pnl_bps);  // Store as positive
            s.lossTimeNs += heldNs;
        } else {  // Scratch
            s.scratches++;
        }
    }
    
    // =========================================================================
    // GET STATS
    // =========================================================================
    const PredatorExpectancyStats& getStats(const std::string& sym) const {
        static const PredatorExpectancyStats empty;
        auto it = stats_.find(sym);
        return (it != stats_.end()) ? it->second : empty;
    }
    
    const std::unordered_map<std::string, PredatorExpectancyStats>& allStats() const {
        return stats_;
    }
    
    // =========================================================================
    // HEALTH CHECK
    // =========================================================================
    bool isSymbolHealthy(const std::string& sym) const {
        auto it = stats_.find(sym);
        return (it == stats_.end()) ? true : it->second.isHealthy();
    }
    
    // =========================================================================
    // PRINT ALL
    // =========================================================================
    void printAll() const {
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║  PREDATOR EXPECTANCY DASHBOARD                                ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        for (const auto& [sym, stats] : stats_) {
            stats.print(sym);
        }
        
        if (stats_.empty()) {
            printf("║  No trades recorded yet                                       ║\n");
        }
        
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    // =========================================================================
    // RESET
    // =========================================================================
    void reset() {
        stats_.clear();
    }
    
    void resetSymbol(const std::string& sym) {
        stats_.erase(sym);
    }
    
    // =========================================================================
    // SINGLETON
    // =========================================================================
    static PredatorExpectancy& instance() {
        static PredatorExpectancy inst;
        return inst;
    }

private:
    std::unordered_map<std::string, PredatorExpectancyStats> stats_;
};

// =========================================================================
// CONVENIENCE FUNCTION
// =========================================================================
inline PredatorExpectancy& getPredatorExpectancy() {
    return PredatorExpectancy::instance();
}

} // namespace Chimera
