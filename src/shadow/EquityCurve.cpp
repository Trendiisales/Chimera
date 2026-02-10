#include "shadow/EquityCurve.hpp"
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <map>

namespace shadow {

std::map<std::string, EquityCurve::SymbolStats> EquityCurve::stats_;
std::mutex EquityCurve::mutex_;

void EquityCurve::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create log directory
    mkdir("/var/log/chimera", 0755);
    
    // Clear stats
    stats_.clear();
    
    printf("[EQUITY_CURVE] Initialized\n");
}

void EquityCurve::update(
    const std::string& symbol,
    double realized,
    double unrealized,
    double fees
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SymbolStats& s = stats_[symbol];
    s.realized_pnl = realized;
    s.unrealized_pnl = unrealized;
    s.fees = fees;
}

void EquityCurve::logTrade(const std::string& symbol, double pnl) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SymbolStats& s = stats_[symbol];
    s.trade_count++;
    
    if (pnl >= 0) {
        s.wins++;
    } else {
        s.losses++;
    }
}

void EquityCurve::exportCSV(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    const char* path = filename.empty() ? DEFAULT_CSV_PATH : filename.c_str();
    
    FILE* f = fopen(path, "w");
    if (!f) {
        perror("EquityCurve::exportCSV");
        return;
    }
    
    // Get current timestamp
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp),
        "%04d-%02d-%02d %02d:%02d:%02d",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec
    );
    
    // Write header
    fprintf(f, "timestamp,symbol,realized_pnl,unrealized_pnl,fees,total_pnl,trade_count,wins,losses,win_rate\n");
    
    // Write per-symbol stats
    for (const auto& [symbol, stats] : stats_) {
        double win_rate = stats.trade_count > 0 
            ? (double)stats.wins / stats.trade_count * 100.0 
            : 0.0;
        
        fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.1f%%\n",
            timestamp,
            symbol.c_str(),
            stats.realized_pnl,
            stats.unrealized_pnl,
            stats.fees,
            stats.totalPnL(),
            stats.trade_count,
            stats.wins,
            stats.losses,
            win_rate
        );
    }
    
    // Write total row
    SymbolStats total = getTotalStatsLocked();
    double total_win_rate = total.trade_count > 0
        ? (double)total.wins / total.trade_count * 100.0
        : 0.0;
    
    fprintf(f, "%s,TOTAL,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.1f%%\n",
        timestamp,
        total.realized_pnl,
        total.unrealized_pnl,
        total.fees,
        total.totalPnL(),
        total.trade_count,
        total.wins,
        total.losses,
        total_win_rate
    );
    
    fclose(f);
    
    printf("[EQUITY_CURVE] Exported to: %s\n", path);
}

EquityCurve::SymbolStats EquityCurve::getStats(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = stats_.find(symbol);
    if (it != stats_.end()) {
        return it->second;
    }
    return SymbolStats{};
}

EquityCurve::SymbolStats EquityCurve::getTotalStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    return getTotalStatsLocked();
}

EquityCurve::SymbolStats EquityCurve::getTotalStatsLocked() {
    SymbolStats total;
    
    for (const auto& [symbol, stats] : stats_) {
        total.realized_pnl += stats.realized_pnl;
        total.unrealized_pnl += stats.unrealized_pnl;
        total.fees += stats.fees;
        total.trade_count += stats.trade_count;
        total.wins += stats.wins;
        total.losses += stats.losses;
    }
    
    return total;
}

void EquityCurve::printSummary() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    printf("\n");
    printf("================================================================================\n");
    printf("EQUITY CURVE SUMMARY\n");
    printf("================================================================================\n");
    printf("%-10s %10s %10s %8s %10s %6s %4s %4s %8s\n",
        "Symbol", "Realized", "Unrealized", "Fees", "Total", "Trades", "W", "L", "WinRate"
    );
    printf("--------------------------------------------------------------------------------\n");
    
    for (const auto& [symbol, stats] : stats_) {
        double win_rate = stats.trade_count > 0
            ? (double)stats.wins / stats.trade_count * 100.0
            : 0.0;
        
        printf("%-10s %10.2f %10.2f %8.2f %10.2f %6d %4d %4d %7.1f%%\n",
            symbol.c_str(),
            stats.realized_pnl,
            stats.unrealized_pnl,
            stats.fees,
            stats.totalPnL(),
            stats.trade_count,
            stats.wins,
            stats.losses,
            win_rate
        );
    }
    
    printf("--------------------------------------------------------------------------------\n");
    
    SymbolStats total;
    for (const auto& [symbol, stats] : stats_) {
        total.realized_pnl += stats.realized_pnl;
        total.unrealized_pnl += stats.unrealized_pnl;
        total.fees += stats.fees;
        total.trade_count += stats.trade_count;
        total.wins += stats.wins;
        total.losses += stats.losses;
    }
    
    double total_win_rate = total.trade_count > 0
        ? (double)total.wins / total.trade_count * 100.0
        : 0.0;
    
    printf("%-10s %10.2f %10.2f %8.2f %10.2f %6d %4d %4d %7.1f%%\n",
        "TOTAL",
        total.realized_pnl,
        total.unrealized_pnl,
        total.fees,
        total.totalPnL(),
        total.trade_count,
        total.wins,
        total.losses,
        total_win_rate
    );
    
    printf("================================================================================\n\n");
}

} // namespace shadow
