// =============================================================================
// TradeRecord.hpp - v4.8.0 - DAILY HEALTH AUDIT SUBSYSTEM
// =============================================================================
// PURPOSE: Trade record structure for daily health audit
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <chrono>

namespace Chimera {

enum class TradeOutcome : uint8_t {
    WIN = 0,
    LOSS = 1,
    SCRATCH = 2
};

inline const char* tradeOutcomeToString(TradeOutcome o) {
    switch (o) {
        case TradeOutcome::WIN:     return "WIN";
        case TradeOutcome::LOSS:    return "LOSS";
        case TradeOutcome::SCRATCH: return "SCRATCH";
        default:                    return "UNKNOWN";
    }
}

struct TradeRecord {
    std::string symbol;
    std::string profile;

    double pnl_r = 0.0;           // PnL in R (risk units)
    double entry_edge = 0.0;
    double exit_edge = 0.0;

    std::chrono::milliseconds duration{0};

    TradeOutcome outcome = TradeOutcome::SCRATCH;
    std::string exit_reason;

    std::chrono::system_clock::time_point timestamp;
    
    // Convenience constructor
    TradeRecord() = default;
    
    TradeRecord(const std::string& sym, const std::string& prof, double pnl,
                double entry_e, double exit_e, std::chrono::milliseconds dur,
                TradeOutcome out, const std::string& exit_r)
        : symbol(sym), profile(prof), pnl_r(pnl), entry_edge(entry_e),
          exit_edge(exit_e), duration(dur), outcome(out), exit_reason(exit_r),
          timestamp(std::chrono::system_clock::now()) {}
};

} // namespace Chimera
