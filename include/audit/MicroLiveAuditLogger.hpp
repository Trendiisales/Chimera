#pragma once
// =============================================================================
// MicroLiveAuditLogger.hpp - v4.10.2 Micro-Live Trade Audit System
// =============================================================================
// MANDATORY LOGGING:
//   For every trade, you must be able to answer these without thinking:
//   - Why did it enter? (VWAP_PULLBACK + conditions)
//   - Why did it exit? (STALL_KILL / PARTIAL / SL / TRAIL)
//   - How many bars was it held?
//
// If any live trade makes you ask "why did that happen?" you pause trading.
// =============================================================================

#include <string>
#include <cstring>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <atomic>
#include <sstream>
#include <cstdio>

#include "engines/IndexE2Engine.hpp"

namespace Chimera {
namespace Audit {

// =============================================================================
// Session Statistics
// =============================================================================
struct MicroLiveStats {
    std::atomic<int> total_trades{0};
    std::atomic<int> wins{0};
    std::atomic<int> losses{0};
    std::atomic<int> partials{0};
    std::atomic<int> stall_kills{0};
    std::atomic<int> stop_losses{0};
    std::atomic<int> trailing_exits{0};
    std::atomic<int> eod_exits{0};
    
    double total_pnl_r = 0.0;
    double total_pnl_dollars = 0.0;
    int total_bars_held = 0;
    int nas100_trades = 0;
    int us30_trades = 0;
    
    double winRate() const {
        int total = wins.load() + losses.load();
        return total > 0 ? (double)wins.load() / total * 100.0 : 0.0;
    }
    
    double avgBarsHeld() const {
        return total_trades.load() > 0 ? (double)total_bars_held / total_trades.load() : 0.0;
    }
    
    void reset() {
        total_trades = 0;
        wins = 0;
        losses = 0;
        partials = 0;
        stall_kills = 0;
        stop_losses = 0;
        trailing_exits = 0;
        eod_exits = 0;
        total_pnl_r = 0.0;
        total_pnl_dollars = 0.0;
        total_bars_held = 0;
        nas100_trades = 0;
        us30_trades = 0;
    }
};

// =============================================================================
// MicroLiveAuditLogger
// =============================================================================
class MicroLiveAuditLogger {
public:
    static MicroLiveAuditLogger& instance() {
        static MicroLiveAuditLogger inst;
        return inst;
    }
    
    bool start(const char* log_dir = "logs/microlive") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Create directory
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", log_dir);
        int ret = system(cmd);
        (void)ret;
        
        // Generate filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/trades_%04d%02d%02d.csv",
                 log_dir, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
        
        trade_file_.open(filename, std::ios::out | std::ios::app);
        if (!trade_file_.is_open()) {
            printf("[AUDIT] ERROR: Could not open trade log: %s\n", filename);
            return false;
        }
        
        // Write header if new file
        trade_file_.seekp(0, std::ios::end);
        if (trade_file_.tellp() == 0) {
            trade_file_ << "timestamp,symbol,side,entry_price,exit_price,size,"
                        << "pnl_r,pnl_dollars,exit_type,bars_held,"
                        << "entry_reason,exit_reason,entry_ts,exit_ts\n";
        }
        
        // Audit log
        snprintf(filename, sizeof(filename), "%s/audit_%04d%02d%02d.log",
                 log_dir, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
        
        audit_file_.open(filename, std::ios::out | std::ios::app);
        
        // Session log (summary)
        snprintf(filename, sizeof(filename), "%s/session.log", log_dir);
        session_file_.open(filename, std::ios::out | std::ios::app);
        
        running_ = true;
        
        logAudit("SYSTEM", "MICROLIVE_START", "Micro-live audit logging started");
        printf("[AUDIT] Trade logging started\n");
        
        return true;
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (running_) {
            printSessionSummary();
            logAudit("SYSTEM", "MICROLIVE_STOP", "Micro-live audit logging stopped");
            
            if (trade_file_.is_open()) trade_file_.close();
            if (audit_file_.is_open()) audit_file_.close();
            if (session_file_.is_open()) session_file_.close();
            
            running_ = false;
        }
    }
    
    void logTrade(const E2::E2TradeRecord& trade) {
        if (!running_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        // CSV entry
        if (trade_file_.is_open()) {
            trade_file_ << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << ","
                        << trade.symbol << ","
                        << (trade.side > 0 ? "LONG" : "SHORT") << ","
                        << std::fixed << std::setprecision(2) << trade.entry_price << ","
                        << trade.exit_price << ","
                        << std::setprecision(4) << trade.size << ","
                        << std::setprecision(2) << trade.pnl_r << ","
                        << trade.pnl_dollars << ","
                        << E2::exitTypeStr(trade.exit_type) << ","
                        << trade.bars_held << ","
                        << trade.entry_reason << ","
                        << trade.exit_reason << ","
                        << trade.entry_ts << ","
                        << trade.exit_ts << "\n";
            trade_file_.flush();
        }
        
        // Update stats
        stats_.total_trades++;
        stats_.total_pnl_r += trade.pnl_r;
        stats_.total_pnl_dollars += trade.pnl_dollars;
        stats_.total_bars_held += trade.bars_held;
        
        if (trade.pnl_dollars >= 0) {
            stats_.wins++;
        } else {
            stats_.losses++;
        }
        
        switch (trade.exit_type) {
            case E2::ExitType::PARTIAL: stats_.partials++; break;
            case E2::ExitType::STALL_KILL: stats_.stall_kills++; break;
            case E2::ExitType::STOP_LOSS: stats_.stop_losses++; break;
            case E2::ExitType::TRAILING: stats_.trailing_exits++; break;
            case E2::ExitType::EOD: stats_.eod_exits++; break;
            default: break;
        }
        
        if (std::strcmp(trade.symbol, "NAS100") == 0) {
            stats_.nas100_trades++;
        } else if (std::strcmp(trade.symbol, "US30") == 0) {
            stats_.us30_trades++;
        }
        
        // Console output (mandatory visibility)
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║ TRADE #%d: %s %s @ %.2f → %.2f\n", 
               stats_.total_trades.load(),
               trade.symbol,
               trade.side > 0 ? "LONG" : "SHORT",
               trade.entry_price, trade.exit_price);
        printf("║ PnL: %.2fR ($%.2f) | Exit: %s | Bars: %d\n",
               trade.pnl_r, trade.pnl_dollars,
               E2::exitTypeStr(trade.exit_type), trade.bars_held);
        printf("║ Entry: %s | Exit: %s\n", trade.entry_reason, trade.exit_reason);
        printf("║ Session: %d trades, %.2fR, %.1f%% WR\n",
               stats_.total_trades.load(), stats_.total_pnl_r, stats_.winRate());
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    void logAudit(const char* symbol, const char* event, const char* details) {
        if (!running_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!audit_file_.is_open()) return;
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm* tm = std::localtime(&time);
        
        audit_file_ << std::put_time(tm, "%H:%M:%S") << "."
                    << std::setfill('0') << std::setw(3) << ms.count() << " | "
                    << symbol << " | " << event << " | " << details << "\n";
        audit_file_.flush();
    }
    
    void printSessionSummary() {
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  MICRO-LIVE SESSION SUMMARY\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  Total Trades: %d\n", stats_.total_trades.load());
        printf("  Win/Loss: %d / %d (%.1f%% WR)\n", 
               stats_.wins.load(), stats_.losses.load(), stats_.winRate());
        printf("  Total PnL: %.2fR ($%.2f)\n", stats_.total_pnl_r, stats_.total_pnl_dollars);
        printf("  Avg Bars Held: %.1f\n", stats_.avgBarsHeld());
        printf("\n  EXIT DISTRIBUTION:\n");
        printf("    Partials:    %d\n", stats_.partials.load());
        printf("    Stall Kills: %d\n", stats_.stall_kills.load());
        printf("    Stop Losses: %d\n", stats_.stop_losses.load());
        printf("    Trailing:    %d\n", stats_.trailing_exits.load());
        printf("    EOD:         %d\n", stats_.eod_exits.load());
        printf("\n  PER-SYMBOL:\n");
        printf("    NAS100: %d trades\n", stats_.nas100_trades);
        printf("    US30:   %d trades\n", stats_.us30_trades);
        printf("═══════════════════════════════════════════════════════════════\n");
        
        // Log to session file
        if (session_file_.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm* tm = std::localtime(&time);
            
            session_file_ << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << " | "
                          << "trades=" << stats_.total_trades.load() << " | "
                          << "wr=" << std::fixed << std::setprecision(1) << stats_.winRate() << "% | "
                          << "pnl_r=" << std::setprecision(2) << stats_.total_pnl_r << " | "
                          << "pnl_$=" << stats_.total_pnl_dollars << " | "
                          << "partials=" << stats_.partials.load() << " | "
                          << "stalls=" << stats_.stall_kills.load() << " | "
                          << "sl=" << stats_.stop_losses.load() << "\n";
            session_file_.flush();
        }
    }
    
    void checkValidation() {
        // MICRO-LIVE SUCCESS CRITERIA (first 20-30 trades)
        // You are not judging P&L yet.
        // You are checking:
        //   - Trade frequency ≈ 1–2/day
        //   - Exit distribution resembles backtest
        //   - No runaway losers
        //   - No "machine-gun" entries
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║ MICRO-LIVE VALIDATION CHECK\n");
        printf("╠═══════════════════════════════════════════════════════════════╣\n");
        
        int total = stats_.total_trades.load();
        
        // Exit distribution check
        int stall_pct = total > 0 ? stats_.stall_kills.load() * 100 / total : 0;
        int sl_pct = total > 0 ? stats_.stop_losses.load() * 100 / total : 0;
        int partial_pct = total > 0 ? stats_.partials.load() * 100 / total : 0;
        
        bool stall_ok = stall_pct >= 20 && stall_pct <= 60;  // Target: 30-45%
        bool sl_ok = sl_pct < 40;  // Should not be all SLs
        bool partial_ok = stats_.partials.load() > 0 || total < 5;  // Some partials expected
        
        printf("║ [%s] Stall Kills: %d%% (expect 30-45%%)\n", 
               stall_ok ? "✓" : "!", stall_pct);
        printf("║ [%s] Stop Losses: %d%% (expect <40%%)\n",
               sl_ok ? "✓" : "!", sl_pct);
        printf("║ [%s] Partials: %d%% (expect >0)\n",
               partial_ok ? "✓" : "!", partial_pct);
        printf("║ [%s] Avg Bars: %.1f (expect ≤7)\n",
               stats_.avgBarsHeld() <= 7 ? "✓" : "!", stats_.avgBarsHeld());
        
        if (stall_ok && sl_ok && partial_ok && stats_.avgBarsHeld() <= 7) {
            printf("╠═══════════════════════════════════════════════════════════════╣\n");
            printf("║ ✅ VALIDATION PASSED - Behavior matches backtest\n");
        } else if (total >= 10) {
            printf("╠═══════════════════════════════════════════════════════════════╣\n");
            printf("║ ⚠️  REVIEW NEEDED - Some metrics differ from backtest\n");
        } else {
            printf("╠═══════════════════════════════════════════════════════════════╣\n");
            printf("║ ⏳ Need more trades for validation (have %d, need 10+)\n", total);
        }
        
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    const MicroLiveStats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }
    
private:
    MicroLiveAuditLogger() = default;
    ~MicroLiveAuditLogger() { stop(); }
    
    MicroLiveAuditLogger(const MicroLiveAuditLogger&) = delete;
    MicroLiveAuditLogger& operator=(const MicroLiveAuditLogger&) = delete;
    
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::ofstream trade_file_;
    std::ofstream audit_file_;
    std::ofstream session_file_;
    MicroLiveStats stats_;
};

inline MicroLiveAuditLogger& getMicroLiveAudit() {
    return MicroLiveAuditLogger::instance();
}

} // namespace Audit
} // namespace Chimera
