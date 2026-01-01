// =============================================================================
// RollingEdgeAudit.hpp - v4.8.0 - ROLLING EDGE AUDIT ENGINE
// =============================================================================
// PURPOSE: Track edge decay over rolling N sessions
//
// GOVERNANCE HIERARCHY (IMPORTANT):
//   1. Latency / Shock / Risk exits (highest priority)
//   2. DailyHealthAudit (hard stop)
//   3. RollingEdgeAudit (slow throttle)  <-- THIS
//   4. Strategy logic
//
// Rolling audit NEVER allows a trade that daily audit would block.
//
// USAGE:
//   RollingEdgeAudit& rolling = getRollingEdgeAudit();
//   rolling.recordTrade(tradeRecord);
//   RollingEdgeReport rep = rolling.evaluateProfile("SCALP_FAST");
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "TradeRecord.hpp"
#include "RollingEdgeReport.hpp"

#include <deque>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Chimera {

class RollingEdgeAudit {
public:
    // =========================================================================
    // TRADE RECORDING
    // =========================================================================
    void recordTrade(const TradeRecord& trade);

    // =========================================================================
    // EVALUATE PROFILE
    // =========================================================================
    RollingEdgeReport evaluateProfile(const std::string& profile) const;
    
    // =========================================================================
    // EVALUATE ALL PROFILES
    // =========================================================================
    std::unordered_map<std::string, RollingEdgeReport> evaluateAll() const;

    // =========================================================================
    // RESET
    // =========================================================================
    void reset();
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    size_t tradeCount(const std::string& profile) const;
    
    // =========================================================================
    // SINGLETON
    // =========================================================================
    static RollingEdgeAudit& instance() {
        static RollingEdgeAudit inst(100);
        return inst;
    }

private:
    explicit RollingEdgeAudit(size_t max_trades) : max_trades_(max_trades) {}
    
    size_t max_trades_;
    mutable std::mutex mutex_;

    std::unordered_map<std::string, std::deque<TradeRecord>> trades_by_profile_;

    RollingEdgeReport computeReport(
        const std::string& profile,
        const std::deque<TradeRecord>& trades
    ) const;
};

// =========================================================================
// CONVENIENCE FUNCTION
// =========================================================================
inline RollingEdgeAudit& getRollingEdgeAudit() {
    return RollingEdgeAudit::instance();
}

} // namespace Chimera
