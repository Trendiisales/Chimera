// =============================================================================
// DailyHealthAudit.hpp - v4.8.0 - DAILY HEALTH AUDIT SUBSYSTEM
// =============================================================================
// PURPOSE: Automatic daily health audit that protects capital
//
// WHAT THIS GIVES YOU:
//   - You cannot hide bad behavior behind PnL
//   - You see failure before drawdown
//   - You know when to stop or scale
//   - You operate like a professional desk
//
// USAGE:
//   DailyHealthAudit& audit = getDailyAudit();
//   audit.recordTrade(tradeRecord);
//   audit.recordVeto(symbol, reason);
//   DailyAuditReport report = audit.runDailyAudit();
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "TradeRecord.hpp"
#include "DailyAuditReport.hpp"

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Chimera {

class DailyHealthAudit {
public:
    // =========================================================================
    // TRADE RECORDING
    // =========================================================================
    void recordTrade(const TradeRecord& trade);
    
    // =========================================================================
    // VETO RECORDING
    // =========================================================================
    void recordVeto(const std::string& symbol, const std::string& reason);
    
    // =========================================================================
    // RUN DAILY AUDIT
    // =========================================================================
    // Returns the audit report with verdict (PASS/WARNING/FAIL)
    // This should be called at end of trading session
    DailyAuditReport runDailyAudit();
    
    // =========================================================================
    // RESET (START OF NEW DAY)
    // =========================================================================
    void reset();
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    size_t tradeCount() const { return trades_.size(); }
    size_t vetoCount() const { return vetoes_.size(); }
    
    // =========================================================================
    // SINGLETON
    // =========================================================================
    static DailyHealthAudit& instance() {
        static DailyHealthAudit inst;
        return inst;
    }

private:
    DailyHealthAudit() = default;
    
    std::vector<TradeRecord> trades_;
    std::vector<std::string> vetoes_;
    std::mutex mutex_;

    // Computation helpers
    double computeAvgLoss() const;
    double computeAvgWin() const;
    double computePayoffRatio() const;

    double computeAvgLosingDuration() const;
    double computeAvgWinningDuration() const;

    double computeMaxTradeLoss() const;
    double computeWorstThreeTradeDD() const;

    bool vetoReasonsSane() const;
    
    int countWins() const;
    int countLosses() const;
    int countScratches() const;
};

// =========================================================================
// CONVENIENCE FUNCTION
// =========================================================================
inline DailyHealthAudit& getDailyAudit() {
    return DailyHealthAudit::instance();
}

} // namespace Chimera
