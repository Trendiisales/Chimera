// =============================================================================
// DailyReportExporter.hpp - v4.8.0 - JSON REPORT EXPORT
// =============================================================================
// PURPOSE: Export daily audit reports to JSON for accountability
//
// You now have a permanent audit trail.
//
// USAGE:
//   DailyReportExporter::exportJson(report, "logs/daily_audit_2025-01-01.json");
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "DailyAuditReport.hpp"

#include <string>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Chimera {

class DailyReportExporter {
public:
    // =========================================================================
    // EXPORT TO JSON FILE
    // =========================================================================
    static bool exportJson(const DailyAuditReport& r, const std::string& path) {
        std::ofstream out(path);
        if (!out.is_open()) {
            printf("[REPORT-EXPORTER] ❌ Failed to open: %s\n", path.c_str());
            return false;
        }

        out << "{\n";
        out << "  \"timestamp\": \"" << getCurrentTimestamp() << "\",\n";
        out << "  \"verdict\": \"" << r.verdict << "\",\n";
        out << "  \"pass\": " << (r.pass ? "true" : "false") << ",\n";
        out << "  \"warning\": " << (r.warning ? "true" : "false") << ",\n";
        out << "  \"fail\": " << (r.fail ? "true" : "false") << ",\n";
        out << "  \"total_trades\": " << r.total_trades << ",\n";
        out << "  \"winning_trades\": " << r.winning_trades << ",\n";
        out << "  \"losing_trades\": " << r.losing_trades << ",\n";
        out << "  \"scratch_trades\": " << r.scratch_trades << ",\n";
        out << "  \"win_rate\": " << r.win_rate << ",\n";
        out << "  \"avg_loss_r\": " << r.avg_loss_r << ",\n";
        out << "  \"avg_win_r\": " << r.avg_win_r << ",\n";
        out << "  \"payoff_ratio\": " << r.payoff_ratio << ",\n";
        out << "  \"avg_losing_duration_sec\": " << r.avg_losing_duration_sec << ",\n";
        out << "  \"avg_winning_duration_sec\": " << r.avg_winning_duration_sec << ",\n";
        out << "  \"max_trade_loss_r\": " << r.max_trade_loss_r << ",\n";
        out << "  \"worst_three_trade_dd_r\": " << r.worst_three_trade_dd_r << ",\n";
        out << "  \"veto_count\": " << r.veto_reasons.size() << "\n";
        out << "}\n";
        
        out.close();
        printf("[REPORT-EXPORTER] ✅ Exported: %s\n", path.c_str());
        return true;
    }
    
    // =========================================================================
    // GET TODAY'S DATE STRING
    // =========================================================================
    static std::string getTodayDate() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }
    
    // =========================================================================
    // GET DEFAULT EXPORT PATH
    // =========================================================================
    static std::string getDefaultPath() {
        return "logs/daily_audit_" + getTodayDate() + ".json";
    }
    
    // =========================================================================
    // EXPORT WITH DEFAULT PATH
    // =========================================================================
    static bool exportToday(const DailyAuditReport& r) {
        return exportJson(r, getDefaultPath());
    }

private:
    static std::string getCurrentTimestamp() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
};

} // namespace Chimera
