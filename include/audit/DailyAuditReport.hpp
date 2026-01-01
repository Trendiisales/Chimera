// =============================================================================
// DailyAuditReport.hpp - v4.8.0 - DAILY HEALTH AUDIT SUBSYSTEM
// =============================================================================
// PURPOSE: Report structure for daily health audit results
//
// HARD RULES (NON-NEGOTIABLE):
//   - avg_loss_r > 1.0 → FAIL
//   - payoff_ratio < 1.5 (with wins) → FAIL
//   - max_trade_loss_r > 1.2 → FAIL
//   - worst_three_trade_dd_r > 2.0 → FAIL
//   - losing duration > 0.5x winning duration → WARNING
//   - insane veto reasons → FAIL
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <cstdio>

namespace Chimera {

struct DailyAuditReport {
    bool pass = true;
    bool warning = false;
    bool fail = false;

    double avg_loss_r = 0.0;
    double avg_win_r = 0.0;
    double payoff_ratio = 0.0;

    double avg_losing_duration_sec = 0.0;
    double avg_winning_duration_sec = 0.0;

    double max_trade_loss_r = 0.0;
    double worst_three_trade_dd_r = 0.0;
    
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    int scratch_trades = 0;
    
    double win_rate = 0.0;

    std::vector<std::string> veto_reasons;

    std::string verdict = "PASS"; // PASS / WARNING / FAIL
    
    // Print report to console
    void print() const {
        const char* verdict_icon = fail ? "❌" : (warning ? "⚠️" : "✅");
        
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║  DAILY HEALTH AUDIT REPORT                                    ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Verdict: %s %-8s                                         ║\n", verdict_icon, verdict.c_str());
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Trades:       %3d total (%d W / %d L / %d S)                  ║\n",
               total_trades, winning_trades, losing_trades, scratch_trades);
        printf("║  Win Rate:     %.1f%%                                          ║\n", win_rate * 100.0);
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Avg Win:      %.2fR                                          ║\n", avg_win_r);
        printf("║  Avg Loss:     %.2fR  %s                                      ║\n", 
               avg_loss_r, avg_loss_r > 1.0 ? "❌" : "✔");
        printf("║  Payoff:       %.2f   %s                                      ║\n",
               payoff_ratio, payoff_ratio < 1.5 ? "❌" : "✔");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Max Loss:     %.2fR  %s                                      ║\n",
               max_trade_loss_r, max_trade_loss_r > 1.2 ? "❌" : "✔");
        printf("║  Worst 3-DD:   %.2fR  %s                                      ║\n",
               worst_three_trade_dd_r, worst_three_trade_dd_r > 2.0 ? "❌" : "✔");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Avg Win Dur:  %.1fs                                          ║\n", avg_winning_duration_sec);
        printf("║  Avg Loss Dur: %.1fs  %s                                      ║\n",
               avg_losing_duration_sec,
               (avg_winning_duration_sec > 0 && avg_losing_duration_sec > 0.5 * avg_winning_duration_sec) ? "⚠️" : "✔");
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    // JSON serialization
    void toJSON(char* buf, size_t buf_size) const {
        snprintf(buf, buf_size,
            "{"
            "\"verdict\":\"%s\","
            "\"pass\":%s,"
            "\"warning\":%s,"
            "\"fail\":%s,"
            "\"total_trades\":%d,"
            "\"winning_trades\":%d,"
            "\"losing_trades\":%d,"
            "\"scratch_trades\":%d,"
            "\"win_rate\":%.4f,"
            "\"avg_loss_r\":%.4f,"
            "\"avg_win_r\":%.4f,"
            "\"payoff_ratio\":%.4f,"
            "\"avg_losing_duration_sec\":%.2f,"
            "\"avg_winning_duration_sec\":%.2f,"
            "\"max_trade_loss_r\":%.4f,"
            "\"worst_three_trade_dd_r\":%.4f"
            "}",
            verdict.c_str(),
            pass ? "true" : "false",
            warning ? "true" : "false",
            fail ? "true" : "false",
            total_trades, winning_trades, losing_trades, scratch_trades,
            win_rate,
            avg_loss_r, avg_win_r, payoff_ratio,
            avg_losing_duration_sec, avg_winning_duration_sec,
            max_trade_loss_r, worst_three_trade_dd_r
        );
    }
};

} // namespace Chimera
