// =============================================================================
// OpenRangeProfile.hpp - v4.9.0 - OPEN RANGE EXPLOITER (ORE) PROFILE
// =============================================================================
// 🎯 ORE — CORE PHILOSOPHY
//
// ORE monetizes NY Open liquidity resolution (09:30-09:35 NY / 13:30-13:35 UTC).
//
// Between 09:30-09:35 NY, institutions:
//   - Set inventory
//   - Defend opening VWAP
//   - Break or reject the opening range
//
// Retail trades breakouts.
// Pros trade acceptance vs rejection.
// ORE trades the resolution of this conflict.
//
// =============================================================================
// ENTRY TYPES:
//
// TYPE A — RANGE BREAK + ACCEPTANCE
//   - Price breaks opening range
//   - Holds > 1.5s outside range
//   - VWAP slope agrees with direction
//   - Order book imbalance confirms
//   → Enter WITH the break
//
// TYPE B — RANGE FAILURE FADE
//   - Price breaks opening range
//   - Fails within 1.0s (returns inside)
//   - VWAP rejects the move
//   - Imbalance flips
//   → Enter AGAINST the failed break (fade)
//
// =============================================================================
// RISK MODEL (NON-NEGOTIABLE):
//   - Risk per trade: 0.15%
//   - Max positions: 1
//   - Trades per symbol per day: 1
//   - Hard time cap: 20s
//   - Symbols: NAS100, US30, SPX500, XAUUSD
//   - Session: NY OPEN ONLY
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "micro/VwapAcceleration.hpp"
#include "risk/LossVelocity.hpp"
#include "shared/SessionDetector.hpp"

#include <string>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Chimera {

// =============================================================================
// ORE STATE MACHINE
// =============================================================================
enum class ORState : uint8_t {
    IDLE = 0,           // Waiting for NY Open window
    RANGE_BUILDING = 1, // First 2 minutes of NY Open - building the range
    ARMED = 2,          // Range built, waiting for break/acceptance/rejection
    IN_TRADE = 3,       // One position only
    DONE = 4            // Traded for this symbol today (no more entries)
};

inline const char* orStateToString(ORState s) {
    switch (s) {
        case ORState::IDLE:           return "IDLE";
        case ORState::RANGE_BUILDING: return "RANGE_BUILDING";
        case ORState::ARMED:          return "ARMED";
        case ORState::IN_TRADE:       return "IN_TRADE";
        case ORState::DONE:           return "DONE";
        default:                      return "UNKNOWN";
    }
}

// =============================================================================
// ORE IDLE REASON
// =============================================================================
enum class ORIdleReason : uint8_t {
    NONE = 0,
    NOT_NY_OPEN_WINDOW,
    RANGE_NOT_BUILT,
    GO_NO_GO_BLOCK,
    LATENCY_UNSTABLE,
    SHOCK_ACTIVE,
    SYMBOL_DISABLED,
    ALREADY_TRADED_TODAY,
    POSITION_OPEN,
    NO_BREAK_DETECTED,
    WAITING_ACCEPTANCE,
    WAITING_REJECTION
};

inline const char* orIdleReasonToString(ORIdleReason r) {
    switch (r) {
        case ORIdleReason::NONE:                 return "NONE";
        case ORIdleReason::NOT_NY_OPEN_WINDOW:   return "NOT_NY_OPEN_WINDOW";
        case ORIdleReason::RANGE_NOT_BUILT:      return "RANGE_NOT_BUILT";
        case ORIdleReason::GO_NO_GO_BLOCK:       return "GO_NO_GO_BLOCK";
        case ORIdleReason::LATENCY_UNSTABLE:     return "LATENCY_UNSTABLE";
        case ORIdleReason::SHOCK_ACTIVE:         return "SHOCK_ACTIVE";
        case ORIdleReason::SYMBOL_DISABLED:      return "SYMBOL_DISABLED";
        case ORIdleReason::ALREADY_TRADED_TODAY: return "ALREADY_TRADED_TODAY";
        case ORIdleReason::POSITION_OPEN:        return "POSITION_OPEN";
        case ORIdleReason::NO_BREAK_DETECTED:    return "NO_BREAK_DETECTED";
        case ORIdleReason::WAITING_ACCEPTANCE:   return "WAITING_ACCEPTANCE";
        case ORIdleReason::WAITING_REJECTION:    return "WAITING_REJECTION";
        default:                                 return "UNKNOWN";
    }
}

inline const char* orIdleReasonIcon(ORIdleReason r) {
    switch (r) {
        case ORIdleReason::NONE:                 return "✓";
        case ORIdleReason::NOT_NY_OPEN_WINDOW:   return "⏰";
        case ORIdleReason::RANGE_NOT_BUILT:      return "📊";
        case ORIdleReason::GO_NO_GO_BLOCK:       return "🚫";
        case ORIdleReason::LATENCY_UNSTABLE:     return "⚡";
        case ORIdleReason::SHOCK_ACTIVE:         return "💥";
        case ORIdleReason::SYMBOL_DISABLED:      return "🔒";
        case ORIdleReason::ALREADY_TRADED_TODAY: return "✔️";
        case ORIdleReason::POSITION_OPEN:        return "📈";
        case ORIdleReason::NO_BREAK_DETECTED:    return "⏳";
        case ORIdleReason::WAITING_ACCEPTANCE:   return "🔄";
        case ORIdleReason::WAITING_REJECTION:    return "↩️";
        default:                                 return "?";
    }
}

// =============================================================================
// TRADE SIDE (shared with Predator)
// =============================================================================
enum class ORSide : uint8_t {
    NONE = 0,
    BUY = 1,
    SELL = 2
};

inline const char* orSideToString(ORSide s) {
    switch (s) {
        case ORSide::BUY:  return "BUY";
        case ORSide::SELL: return "SELL";
        default:           return "NONE";
    }
}

// =============================================================================
// ORE SYMBOL CONFIG
// =============================================================================
struct ORSymbolConfig {
    bool enabled = false;
    double minRangePoints = 5.0;     // Minimum range size (points)
    double maxRangePoints = 50.0;    // Maximum range size (points)
    double acceptanceHoldSec = 1.5;  // Must hold outside range this long
    double rejectionTimeSec = 1.0;   // Failure must happen within this time
    double minImbalance = 0.60;      // Minimum OB imbalance for confirmation
    uint64_t maxHoldNs = 20'000'000'000ULL;  // 20 second time cap
    double pointValue = 1.0;         // Point value for PnL
};

inline ORSymbolConfig getORConfig(const char* symbol) {
    ORSymbolConfig cfg;
    
    if (strcmp(symbol, "NAS100") == 0) {
        cfg.enabled = true;
        cfg.minRangePoints = 10.0;
        cfg.maxRangePoints = 80.0;
        cfg.acceptanceHoldSec = 1.5;
        cfg.rejectionTimeSec = 1.0;
        cfg.minImbalance = 0.65;
        cfg.maxHoldNs = 20'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "US30") == 0) {
        cfg.enabled = true;
        cfg.minRangePoints = 20.0;
        cfg.maxRangePoints = 150.0;
        cfg.acceptanceHoldSec = 1.5;
        cfg.rejectionTimeSec = 1.0;
        cfg.minImbalance = 0.60;
        cfg.maxHoldNs = 20'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "SPX500") == 0) {
        cfg.enabled = true;
        cfg.minRangePoints = 3.0;
        cfg.maxRangePoints = 25.0;
        cfg.acceptanceHoldSec = 1.5;
        cfg.rejectionTimeSec = 1.0;
        cfg.minImbalance = 0.65;
        cfg.maxHoldNs = 20'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "XAUUSD") == 0) {
        cfg.enabled = true;
        cfg.minRangePoints = 2.0;
        cfg.maxRangePoints = 15.0;
        cfg.acceptanceHoldSec = 2.0;
        cfg.rejectionTimeSec = 1.2;
        cfg.minImbalance = 0.55;
        cfg.maxHoldNs = 25'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    
    return cfg;
}

inline bool isORSymbolEnabled(const char* symbol) {
    return getORConfig(symbol).enabled;
}

// =============================================================================
// MARKET SNAPSHOT (INPUT TO ORE)
// =============================================================================
struct ORMarketSnapshot {
    // Identification
    const char* symbol = "";
    uint64_t now_ns = 0;
    
    // Price data
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    double vwap = 0.0;
    double vwapSlope = 0.0;
    
    // Order book
    double imbalance = 0.0;          // -1.0 to +1.0
    
    // State flags
    bool latencyStable = false;
    bool shockActive = false;
    bool goNoGoIsGo = false;
    
    // Session timing
    bool isNYOpenWindow = false;     // True if 13:30-13:35 UTC
    bool isNYSession = false;        // True if in NY session
    
    // Computed helpers
    double spread() const { return ask - bid; }
};

// =============================================================================
// OPENING RANGE STATE (per symbol)
// =============================================================================
struct OpeningRange {
    double high = 0.0;
    double low = 0.0;
    double open = 0.0;
    double vwap = 0.0;
    uint64_t buildStartNs = 0;
    uint64_t buildEndNs = 0;
    bool isValid = false;
    
    double range() const { return high - low; }
    double midpoint() const { return (high + low) / 2.0; }
    
    void reset() {
        high = 0.0;
        low = 0.0;
        open = 0.0;
        vwap = 0.0;
        buildStartNs = 0;
        buildEndNs = 0;
        isValid = false;
    }
};

// =============================================================================
// BREAK TRACKING
// =============================================================================
struct BreakState {
    bool breakDetected = false;
    int breakDirection = 0;          // +1 = broke high, -1 = broke low
    double breakPrice = 0.0;
    uint64_t breakTimestampNs = 0;
    bool acceptanceConfirmed = false;
    bool rejectionConfirmed = false;
    
    void reset() {
        breakDetected = false;
        breakDirection = 0;
        breakPrice = 0.0;
        breakTimestampNs = 0;
        acceptanceConfirmed = false;
        rejectionConfirmed = false;
    }
};

// =============================================================================
// OPEN RANGE PROFILE
// =============================================================================
class OpenRangeProfile {
public:
    OpenRangeProfile();
    
    // =========================================================================
    // MAIN TICK HANDLER
    // =========================================================================
    void onTick(const ORMarketSnapshot& snap);
    
    // =========================================================================
    // FILL HANDLER
    // =========================================================================
    void onFill(double fillPrice, double qty, bool isBuy);
    
    // =========================================================================
    // POSITION CLOSE HANDLER
    // =========================================================================
    void onPositionClosed(double pnl_bps, uint64_t heldNs);
    
    // =========================================================================
    // TIMER (for periodic checks)
    // =========================================================================
    void onTimer(uint64_t now_ns);
    
    // =========================================================================
    // NEW DAY RESET (call at midnight)
    // =========================================================================
    void resetDay();
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    const char* name() const { return "OPEN_RANGE"; }
    ORState state() const { return state_; }
    ORIdleReason idleReason() const { return idleReason_; }
    bool hasPosition() const { return hasPosition_; }
    int tradesThisDay() const { return tradesThisDay_; }
    const OpeningRange& getRange(const char* symbol) const;
    
    // =========================================================================
    // STATUS OUTPUT
    // =========================================================================
    void printStatus() const;
    void toJSON(char* buf, size_t buf_size) const;
    
    // =========================================================================
    // ENABLE/DISABLE
    // =========================================================================
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; state_ = ORState::IDLE; }
    bool isEnabled() const { return enabled_; }

private:
    // Global state
    ORState state_ = ORState::IDLE;
    ORIdleReason idleReason_ = ORIdleReason::NONE;
    uint64_t stateTs_ns_ = 0;
    
    // Per-symbol opening range
    std::unordered_map<std::string, OpeningRange> ranges_;
    std::unordered_map<std::string, BreakState> breaks_;
    
    // Symbols that have traded today (max 1 per day per symbol)
    std::unordered_set<std::string> tradedToday_;
    
    // Position tracking
    bool hasPosition_ = false;
    ORSide positionSide_ = ORSide::NONE;
    double entryPrice_ = 0.0;
    uint64_t tradeStartNs_ = 0;
    std::string currentSymbol_;
    
    // Daily tracking
    int tradesThisDay_ = 0;
    
    // VWAP tracking
    VwapState vwapState_;
    
    // Enable flag
    std::atomic<bool> enabled_{true};
    
    // Default empty range for const reference
    static OpeningRange emptyRange_;
    
    // =========================================================================
    // RANGE BUILDING CONSTANTS
    // =========================================================================
    static constexpr uint64_t RANGE_BUILD_DURATION_NS = 2ULL * 60 * 1'000'000'000ULL;  // 2 minutes
    
    // =========================================================================
    // INTERNAL METHODS
    // =========================================================================
    void buildRange(const ORMarketSnapshot& snap);
    void evaluateEntry(const ORMarketSnapshot& snap);
    void evaluateExit(const ORMarketSnapshot& snap);
    
    bool detectBreak(const ORMarketSnapshot& snap);
    bool checkAcceptance(const ORMarketSnapshot& snap);
    bool checkRejection(const ORMarketSnapshot& snap);
    
    bool hardGatesPass(const ORMarketSnapshot& snap);
    bool isNYOpenWindow(uint64_t now_ns) const;
    
    // Output (virtual for override in actual implementation)
    virtual void submitOrder(ORSide side, double qty, const char* symbol) {
        printf("[ORE] SUBMIT %s %.6f %s\n", orSideToString(side), qty, symbol);
    }
    
    virtual void closePosition(const char* reason) {
        printf("[ORE] CLOSE: %s\n", reason);
    }
    
    // Real time function - uses steady_clock
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // Real sizing function - Open Range uses 0.15% risk
    double calculateQty(double riskPct, const ORMarketSnapshot& snap) {
        double spreadPts = snap.spread();
        if (spreadPts <= 0) spreadPts = 0.0001;
        
        // Open range trades use moderate stops based on range
        double riskAmount = 10000.0 * riskPct;
        double stopDistPts = spreadPts * 4.0;  // 4x spread for range trades
        double pointValue = 1.0;
        
        if (strstr(snap.symbol, "NAS") || strstr(snap.symbol, "SPX")) {
            pointValue = 1.0;
        } else if (strstr(snap.symbol, "XAU")) {
            pointValue = 0.1;
        }
        
        double qty = riskAmount / (stopDistPts * pointValue * 100.0);
        return std::clamp(qty, 0.01, 8.0);
    }
    
    void enterTrade(ORSide side, const ORMarketSnapshot& snap, const char* reason);
    void exitTrade(const char* reason, const ORMarketSnapshot& snap);
};

} // namespace Chimera
