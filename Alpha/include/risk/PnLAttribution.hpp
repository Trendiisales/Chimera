// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - PnL Attribution Logger
// ═══════════════════════════════════════════════════════════════════════════════
// THIS IS WHAT SEPARATES PROS FROM GAMBLERS
// 
// We attribute every trade by:
// - Symbol
// - Session
// - Hold time
// - R-multiple
// - Scaled or not
// - Entry reason
// - Exit reason
// - Volatility state
// 
// This file alone will tell you:
// - Which session prints money
// - Which symbol deserves capital
// - Whether scaling is worth it
// - Which exits are premature
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <fstream>
#include <string>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "../session/SessionDetector.hpp"

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// PNL RECORD STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════════

struct PnLRecord {
    // Core identifiers
    uint64_t timestamp_ns;
    std::string symbol;
    
    // Trade details
    int side;               // +1 long, -1 short
    double entry_price;
    double exit_price;
    double size;
    
    // Performance
    double r_multiple;
    double pnl_usd;
    double pnl_bps;
    
    // Context
    SessionType session;
    std::string regime;     // TRENDING/RANGING/VOLATILE/QUIET
    double atr_ratio;
    double spread_at_entry;
    double edge_at_entry;
    
    // Execution
    uint64_t hold_ms;
    bool scaled;
    bool moved_to_breakeven;
    
    // Reasons
    std::string entry_reason;
    std::string exit_reason;
};

// ═══════════════════════════════════════════════════════════════════════════════
// PNL LOGGER (SINGLETON)
// ═══════════════════════════════════════════════════════════════════════════════

class PnLLogger {
public:
    static PnLLogger& instance() {
        static PnLLogger logger;
        return logger;
    }
    
    void log_trade(const PnLRecord& rec) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Ensure file is open
        if (!file_.is_open()) {
            open_file();
        }
        
        // Write record
        file_ << format_timestamp(rec.timestamp_ns) << ","
              << rec.symbol << ","
              << (rec.side > 0 ? "LONG" : "SHORT") << ","
              << std::fixed << std::setprecision(5) << rec.entry_price << ","
              << rec.exit_price << ","
              << std::setprecision(4) << rec.size << ","
              << std::setprecision(3) << rec.r_multiple << ","
              << std::setprecision(2) << rec.pnl_usd << ","
              << std::setprecision(2) << rec.pnl_bps << ","
              << session_type_str(rec.session) << ","
              << rec.regime << ","
              << std::setprecision(2) << rec.atr_ratio << ","
              << std::setprecision(4) << rec.spread_at_entry << ","
              << std::setprecision(2) << rec.edge_at_entry << ","
              << rec.hold_ms << ","
              << (rec.scaled ? "1" : "0") << ","
              << (rec.moved_to_breakeven ? "1" : "0") << ","
              << rec.entry_reason << ","
              << rec.exit_reason
              << "\n";
        
        file_.flush();
        
        // Also log to console for live monitoring
        log_to_console(rec);
    }
    
    void set_filename(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
        filename_ = filename;
    }

private:
    PnLLogger() {
        // Generate default filename with date
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream ss;
        ss << "pnl_attribution_" 
           << std::put_time(&tm, "%Y%m%d") 
           << ".csv";
        filename_ = ss.str();
    }
    
    void open_file() {
        file_.open(filename_, std::ios::app);
        
        // Write header if file is new/empty
        file_.seekp(0, std::ios::end);
        if (file_.tellp() == 0) {
            write_header();
        }
    }
    
    void write_header() {
        file_ << "timestamp,symbol,side,entry_price,exit_price,size,"
              << "r_multiple,pnl_usd,pnl_bps,session,regime,atr_ratio,"
              << "spread,edge,hold_ms,scaled,breakeven,entry_reason,exit_reason\n";
    }
    
    std::string format_timestamp(uint64_t ns) {
        uint64_t sec = ns / 1'000'000'000ULL;
        std::time_t t = static_cast<std::time_t>(sec);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    void log_to_console(const PnLRecord& rec) {
        const char* result = rec.r_multiple >= 0 ? "✅" : "❌";
        const char* side = rec.side > 0 ? "LONG" : "SHORT";
        
        std::cerr << "[PNL] " << result << " " << rec.symbol << " " << side
                  << " | R=" << std::fixed << std::setprecision(2) << rec.r_multiple
                  << " | $" << std::setprecision(2) << rec.pnl_usd
                  << " | " << rec.hold_ms << "ms"
                  << " | " << session_type_str(rec.session)
                  << " | " << rec.exit_reason << "\n";
    }
    
    std::mutex mutex_;
    std::ofstream file_;
    std::string filename_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// CONVENIENCE FUNCTION
// ═══════════════════════════════════════════════════════════════════════════════

inline void log_pnl(const PnLRecord& rec) {
    PnLLogger::instance().log_trade(rec);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION ATTRIBUTION SUMMARY
// ═══════════════════════════════════════════════════════════════════════════════
// Call this to analyze which sessions are profitable

struct SessionSummary {
    int trades = 0;
    double total_r = 0.0;
    int wins = 0;
    double total_hold_ms = 0;
    int scaled_wins = 0;
    
    double expectancy() const {
        return trades > 0 ? total_r / trades : 0.0;
    }
    
    double win_rate() const {
        return trades > 0 ? static_cast<double>(wins) / trades : 0.0;
    }
};

}  // namespace Alpha
