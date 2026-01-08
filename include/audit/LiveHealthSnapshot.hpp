// =============================================================================
// LiveHealthSnapshot.hpp - v4.8.0 - LIVE DASHBOARD FEED
// =============================================================================
// PURPOSE: Real-time health snapshot for dashboard broadcast
//
// You see failure before it hurts you.
//
// USAGE:
//   LiveHealthSnapshot snap = LiveHealthSnapshot::fromReport(report);
//   gui.publish("daily_health", snap);
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "DailyAuditReport.hpp"
#include <string>
#include <cstdio>

namespace Chimera {

struct LiveHealthSnapshot {
    std::string verdict = "PASS";
    double avg_loss_r = 0.0;
    double payoff_ratio = 0.0;
    double max_trade_loss_r = 0.0;
    double worst_dd_r = 0.0;
    
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    double win_rate = 0.0;
    
    // =========================================================================
    // CREATE FROM AUDIT REPORT
    // =========================================================================
    static LiveHealthSnapshot fromReport(const DailyAuditReport& r) {
        LiveHealthSnapshot snap;
        snap.verdict = r.verdict;
        snap.avg_loss_r = r.avg_loss_r;
        snap.payoff_ratio = r.payoff_ratio;
        snap.max_trade_loss_r = r.max_trade_loss_r;
        snap.worst_dd_r = r.worst_three_trade_dd_r;
        snap.total_trades = r.total_trades;
        snap.winning_trades = r.winning_trades;
        snap.losing_trades = r.losing_trades;
        snap.win_rate = r.win_rate;
        return snap;
    }
    
    // =========================================================================
    // JSON SERIALIZATION (for WebSocket broadcast)
    // =========================================================================
    void toJSON(char* buf, size_t buf_size) const {
        snprintf(buf, buf_size,
            "{"
            "\"type\":\"daily_health\","
            "\"verdict\":\"%s\","
            "\"avg_loss_r\":%.4f,"
            "\"payoff_ratio\":%.4f,"
            "\"max_trade_loss_r\":%.4f,"
            "\"worst_dd_r\":%.4f,"
            "\"total_trades\":%d,"
            "\"winning_trades\":%d,"
            "\"losing_trades\":%d,"
            "\"win_rate\":%.4f"
            "}",
            verdict.c_str(),
            avg_loss_r,
            payoff_ratio,
            max_trade_loss_r,
            worst_dd_r,
            total_trades,
            winning_trades,
            losing_trades,
            win_rate
        );
    }
    
    // =========================================================================
    // PRINT TO CONSOLE
    // =========================================================================
    void print() const {
        const char* icon = (verdict == "FAIL") ? "❌" : 
                          (verdict == "WARNING") ? "⚠️" : "✅";
        
        printf("[HEALTH] %s %s | Trades: %d (W:%d L:%d) | "
               "AvgLoss: %.2fR | Payoff: %.2f | MaxLoss: %.2fR | DD: %.2fR\n",
               icon, verdict.c_str(),
               total_trades, winning_trades, losing_trades,
               avg_loss_r, payoff_ratio, max_trade_loss_r, worst_dd_r);
    }
};

// =========================================================================
// PER-SYMBOL HEALTH (OPTIONAL EXTENSION)
// =========================================================================
struct SymbolHealthSnapshot {
    std::string symbol;
    double avg_loss_r = 0.0;
    double payoff_ratio = 0.0;
    double worst_dd_r = 0.0;
    int total_trades = 0;
    int veto_count = 0;
    std::string dominant_veto_reason;
    
    void toJSON(char* buf, size_t buf_size) const {
        snprintf(buf, buf_size,
            "{"
            "\"symbol\":\"%s\","
            "\"avg_loss_r\":%.4f,"
            "\"payoff_ratio\":%.4f,"
            "\"worst_dd_r\":%.4f,"
            "\"total_trades\":%d,"
            "\"veto_count\":%d,"
            "\"dominant_veto\":\"%s\""
            "}",
            symbol.c_str(),
            avg_loss_r,
            payoff_ratio,
            worst_dd_r,
            total_trades,
            veto_count,
            dominant_veto_reason.c_str()
        );
    }
};

} // namespace Chimera
