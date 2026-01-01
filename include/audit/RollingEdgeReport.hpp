// =============================================================================
// RollingEdgeReport.hpp - v4.8.0 - ROLLING EDGE AUDIT SUBSYSTEM
// =============================================================================
// PURPOSE: Report structure for rolling edge audit over last N sessions
//
// WHAT THIS ANSWERS:
//   Is this system's edge still alive over the last N sessions —
//   even if daily audits pass?
//
// PROTECTS AGAINST:
//   - Slow edge decay
//   - Regime drift
//   - Over-scratching
//   - "looks fine daily but dying monthly"
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <cstdio>
#include <cstdint>

namespace Chimera {

enum class RollingEdgeVerdict : uint8_t {
    HEALTHY = 0,
    DEGRADING = 1,
    BROKEN = 2
};

inline const char* rollingEdgeVerdictToString(RollingEdgeVerdict v) {
    switch (v) {
        case RollingEdgeVerdict::HEALTHY:   return "HEALTHY";
        case RollingEdgeVerdict::DEGRADING: return "DEGRADING";
        case RollingEdgeVerdict::BROKEN:    return "BROKEN";
        default:                            return "UNKNOWN";
    }
}

struct RollingEdgeReport {
    std::string profile;

    double avg_edge_entry = 0.0;
    double avg_edge_exit = 0.0;
    double edge_retention = 0.0;   // exit / entry (target: > 0.65)

    double win_rate = 0.0;
    double payoff_ratio = 0.0;

    double avg_pnl_r = 0.0;
    double max_drawdown_r = 0.0;
    
    int trade_count = 0;

    RollingEdgeVerdict verdict = RollingEdgeVerdict::HEALTHY;
    
    // =========================================================================
    // PRINT TO CONSOLE
    // =========================================================================
    void print() const {
        const char* icon = verdict == RollingEdgeVerdict::BROKEN ? "❌" :
                          verdict == RollingEdgeVerdict::DEGRADING ? "⚠️" : "✅";
        
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║  ROLLING EDGE REPORT: %-20s                 ║\n", profile.c_str());
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Verdict: %s %-12s                                     ║\n", 
               icon, rollingEdgeVerdictToString(verdict));
        printf("║  Trades:  %d                                                  ║\n", trade_count);
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Entry Edge:     %.4f                                        ║\n", avg_edge_entry);
        printf("║  Exit Edge:      %.4f                                        ║\n", avg_edge_exit);
        printf("║  Edge Retention: %.1f%%  %s                                   ║\n",
               edge_retention * 100.0, edge_retention < 0.55 ? "❌" : (edge_retention < 0.65 ? "⚠️" : "✔"));
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Win Rate:       %.1f%%                                       ║\n", win_rate * 100.0);
        printf("║  Payoff Ratio:   %.2f  %s                                     ║\n",
               payoff_ratio, payoff_ratio < 1.3 ? "❌" : (payoff_ratio < 1.5 ? "⚠️" : "✔"));
        printf("║  Avg PnL:        %.2fR                                        ║\n", avg_pnl_r);
        printf("║  Max Drawdown:   %.2fR  %s                                    ║\n",
               max_drawdown_r, max_drawdown_r > 3.0 ? "❌" : "✔");
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    // =========================================================================
    // JSON SERIALIZATION
    // =========================================================================
    void toJSON(char* buf, size_t buf_size) const {
        snprintf(buf, buf_size,
            "{"
            "\"profile\":\"%s\","
            "\"verdict\":\"%s\","
            "\"trade_count\":%d,"
            "\"avg_edge_entry\":%.6f,"
            "\"avg_edge_exit\":%.6f,"
            "\"edge_retention\":%.4f,"
            "\"win_rate\":%.4f,"
            "\"payoff_ratio\":%.4f,"
            "\"avg_pnl_r\":%.4f,"
            "\"max_drawdown_r\":%.4f"
            "}",
            profile.c_str(),
            rollingEdgeVerdictToString(verdict),
            trade_count,
            avg_edge_entry, avg_edge_exit, edge_retention,
            win_rate, payoff_ratio, avg_pnl_r, max_drawdown_r
        );
    }
};

} // namespace Chimera
