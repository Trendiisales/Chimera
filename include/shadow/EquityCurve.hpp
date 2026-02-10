#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace shadow {

/**
 * EquityCurve - Track and export equity curve to CSV
 * 
 * Features:
 * - Per-symbol PnL tracking
 * - Realized + unrealized PnL
 * - Fee tracking
 * - Trade count
 * - Win/loss tracking
 * - CSV export
 * 
 * CSV Format:
 *   timestamp,symbol,realized_pnl,unrealized_pnl,fees,total_pnl,trade_count,wins,losses
 * 
 * Usage:
 *   EquityCurve::init();
 *   EquityCurve::update(symbol, realized, unrealized, fees);
 *   EquityCurve::logTrade(symbol, pnl);
 *   EquityCurve::exportCSV();
 */
class EquityCurve {
public:
    struct SymbolStats {
        double realized_pnl = 0.0;
        double unrealized_pnl = 0.0;
        double fees = 0.0;
        int trade_count = 0;
        int wins = 0;
        int losses = 0;
        
        double totalPnL() const {
            return realized_pnl + unrealized_pnl - fees;
        }
    };
    
    // Initialize equity curve tracker
    static void init();
    
    // Update symbol stats
    static void update(
        const std::string& symbol,
        double realized,
        double unrealized,
        double fees
    );
    
    // Log a completed trade
    static void logTrade(const std::string& symbol, double pnl);
    
    // Export to CSV
    static void exportCSV(const std::string& filename = "");
    
    // Get stats for a symbol
    static SymbolStats getStats(const std::string& symbol);
    
    // Get total across all symbols
    static SymbolStats getTotalStats();
    
    // Print summary to console
    static void printSummary();
    
private:
    static constexpr const char* DEFAULT_CSV_PATH = "/var/log/chimera/equity_curve.csv";
    
    static SymbolStats getTotalStatsLocked();  // Helper for getTotalStats()
    
    static std::map<std::string, SymbolStats> stats_;
    static std::mutex mutex_;
};

} // namespace shadow
