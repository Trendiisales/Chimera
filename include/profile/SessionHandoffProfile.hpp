// =============================================================================
// SessionHandoffProfile.hpp - v4.9.0 - SESSION HANDOFF CONTINUATION PROFILE
// =============================================================================
// 🎯 SESSION HANDOFF — CORE PHILOSOPHY
//
// This profile monetizes INSTITUTIONAL REPOSITIONING at session boundaries.
//
// Session transitions:
//   - Asia → London
//   - London → NY
//
// This is NOT scalping. This is:
//   - Low frequency (1-2 trades per day)
//   - High quality (extremely clean equity curve)
//   - Structure-based (not noise-based)
//
// =============================================================================
// STATE MACHINE:
//
// IDLE → SESSION_END_OBSERVED → HANDOFF_ARMED → IN_TRADE → DONE
//
// =============================================================================
// ENTRY LOGIC:
//
// 1. DETERMINE BIAS (from prior session):
//    - VWAP hold or reject
//    - Value migration (POC drift)
//    - Failed extremes (high/low rejection)
//
// 2. ENTRY CONDITIONS:
//    - New session opens
//    - Price confirms bias
//    - VWAP supports direction
//    - Only ONE attempt per session
//
// =============================================================================
// EXIT LOGIC:
//
// - Time cap: 60s (this is a structure trade, not a scalp)
// - VWAP reclaim against position
// - Structure failure (failed extreme)
//
// =============================================================================
// RISK MODEL (NON-NEGOTIABLE):
//   - Risk per trade: 0.20%
//   - Trades per day: 1-2 (max 1 per handoff)
//   - Symbols: Indices + Gold
//   - Sessions: Asia→London, London→NY handoffs ONLY
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "micro/VwapAcceleration.hpp"
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
// SESSION HANDOFF STATE MACHINE
// =============================================================================
enum class SHState : uint8_t {
    IDLE = 0,                 // Waiting for session transition
    OBSERVING = 1,            // Observing prior session for bias
    ARMED = 2,                // Bias determined, waiting for new session
    IN_TRADE = 3,             // Position open
    DONE = 4                  // Traded this handoff (no more entries)
};

inline const char* shStateToString(SHState s) {
    switch (s) {
        case SHState::IDLE:      return "IDLE";
        case SHState::OBSERVING: return "OBSERVING";
        case SHState::ARMED:     return "ARMED";
        case SHState::IN_TRADE:  return "IN_TRADE";
        case SHState::DONE:      return "DONE";
        default:                 return "UNKNOWN";
    }
}

// =============================================================================
// SESSION HANDOFF IDLE REASON
// =============================================================================
enum class SHIdleReason : uint8_t {
    NONE = 0,
    NOT_HANDOFF_WINDOW,
    GO_NO_GO_BLOCK,
    LATENCY_UNSTABLE,
    SHOCK_ACTIVE,
    SYMBOL_DISABLED,
    NO_BIAS_DETERMINED,
    WAITING_SESSION_OPEN,
    BIAS_NOT_CONFIRMED,
    POSITION_OPEN,
    ALREADY_TRADED_HANDOFF
};

inline const char* shIdleReasonToString(SHIdleReason r) {
    switch (r) {
        case SHIdleReason::NONE:                   return "NONE";
        case SHIdleReason::NOT_HANDOFF_WINDOW:     return "NOT_HANDOFF_WINDOW";
        case SHIdleReason::GO_NO_GO_BLOCK:         return "GO_NO_GO_BLOCK";
        case SHIdleReason::LATENCY_UNSTABLE:       return "LATENCY_UNSTABLE";
        case SHIdleReason::SHOCK_ACTIVE:           return "SHOCK_ACTIVE";
        case SHIdleReason::SYMBOL_DISABLED:        return "SYMBOL_DISABLED";
        case SHIdleReason::NO_BIAS_DETERMINED:     return "NO_BIAS_DETERMINED";
        case SHIdleReason::WAITING_SESSION_OPEN:   return "WAITING_SESSION_OPEN";
        case SHIdleReason::BIAS_NOT_CONFIRMED:     return "BIAS_NOT_CONFIRMED";
        case SHIdleReason::POSITION_OPEN:          return "POSITION_OPEN";
        case SHIdleReason::ALREADY_TRADED_HANDOFF: return "ALREADY_TRADED_HANDOFF";
        default:                                   return "UNKNOWN";
    }
}

inline const char* shIdleReasonIcon(SHIdleReason r) {
    switch (r) {
        case SHIdleReason::NONE:                   return "✓";
        case SHIdleReason::NOT_HANDOFF_WINDOW:     return "⏰";
        case SHIdleReason::GO_NO_GO_BLOCK:         return "🚫";
        case SHIdleReason::LATENCY_UNSTABLE:       return "⚡";
        case SHIdleReason::SHOCK_ACTIVE:           return "💥";
        case SHIdleReason::SYMBOL_DISABLED:        return "🔒";
        case SHIdleReason::NO_BIAS_DETERMINED:     return "🔍";
        case SHIdleReason::WAITING_SESSION_OPEN:   return "⏳";
        case SHIdleReason::BIAS_NOT_CONFIRMED:     return "❓";
        case SHIdleReason::POSITION_OPEN:          return "📈";
        case SHIdleReason::ALREADY_TRADED_HANDOFF: return "✔️";
        default:                                   return "?";
    }
}

// =============================================================================
// TRADE SIDE
// =============================================================================
enum class SHSide : uint8_t {
    NONE = 0,
    BUY = 1,
    SELL = 2
};

inline const char* shSideToString(SHSide s) {
    switch (s) {
        case SHSide::BUY:  return "BUY";
        case SHSide::SELL: return "SELL";
        default:           return "NONE";
    }
}

// =============================================================================
// BIAS TYPE
// =============================================================================
enum class BiasType : uint8_t {
    NONE = 0,
    BULLISH = 1,   // Bias to buy
    BEARISH = 2    // Bias to sell
};

inline const char* biasToString(BiasType b) {
    switch (b) {
        case BiasType::BULLISH: return "BULLISH";
        case BiasType::BEARISH: return "BEARISH";
        default:                return "NONE";
    }
}

// =============================================================================
// HANDOFF TYPE
// =============================================================================
enum class HandoffType : uint8_t {
    NONE = 0,
    ASIA_TO_LONDON = 1,
    LONDON_TO_NY = 2
};

inline const char* handoffToString(HandoffType h) {
    switch (h) {
        case HandoffType::ASIA_TO_LONDON: return "ASIA→LDN";
        case HandoffType::LONDON_TO_NY:   return "LDN→NY";
        default:                          return "NONE";
    }
}

// =============================================================================
// SESSION HANDOFF SYMBOL CONFIG
// =============================================================================
struct SHSymbolConfig {
    bool enabled = false;
    double minBiasStrength = 0.6;        // Minimum bias confidence
    double vwapConfirmPct = 0.002;       // VWAP must confirm by this %
    uint64_t maxHoldNs = 60'000'000'000ULL;  // 60 second time cap
    double pointValue = 1.0;
};

inline SHSymbolConfig getSHConfig(const char* symbol) {
    SHSymbolConfig cfg;
    
    if (strcmp(symbol, "NAS100") == 0) {
        cfg.enabled = true;
        cfg.minBiasStrength = 0.65;
        cfg.vwapConfirmPct = 0.0015;
        cfg.maxHoldNs = 60'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "US30") == 0) {
        cfg.enabled = true;
        cfg.minBiasStrength = 0.60;
        cfg.vwapConfirmPct = 0.0012;
        cfg.maxHoldNs = 60'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "SPX500") == 0) {
        cfg.enabled = true;
        cfg.minBiasStrength = 0.70;
        cfg.vwapConfirmPct = 0.0020;
        cfg.maxHoldNs = 50'000'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "XAUUSD") == 0) {
        cfg.enabled = true;
        cfg.minBiasStrength = 0.55;
        cfg.vwapConfirmPct = 0.0018;
        cfg.maxHoldNs = 75'000'000'000ULL;  // Gold gets more time
        cfg.pointValue = 1.0;
    }
    
    return cfg;
}

inline bool isSHSymbolEnabled(const char* symbol) {
    return getSHConfig(symbol).enabled;
}

// =============================================================================
// MARKET SNAPSHOT (INPUT TO SESSION HANDOFF)
// =============================================================================
struct SHMarketSnapshot {
    // Identification
    const char* symbol = "";
    uint64_t now_ns = 0;
    
    // Price data
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    double vwap = 0.0;
    double vwapSlope = 0.0;
    
    // Session structure
    double sessionHigh = 0.0;
    double sessionLow = 0.0;
    double sessionOpen = 0.0;
    double priorSessionClose = 0.0;
    double priorSessionVwap = 0.0;
    
    // State flags
    bool latencyStable = false;
    bool shockActive = false;
    bool goNoGoIsGo = false;
    const char* currentSession = "";
    const char* priorSession = "";
    
    // Computed helpers
    double spread() const { return ask - bid; }
};

// =============================================================================
// PRIOR SESSION ANALYSIS (for bias determination)
// =============================================================================
struct PriorSessionAnalysis {
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double vwap = 0.0;
    double poc = 0.0;            // Point of Control
    bool highRejected = false;   // High was tested and rejected
    bool lowRejected = false;    // Low was tested and rejected
    double valueMigration = 0.0; // POC drift (+up, -down)
    
    void reset() {
        high = 0.0;
        low = 0.0;
        close = 0.0;
        vwap = 0.0;
        poc = 0.0;
        highRejected = false;
        lowRejected = false;
        valueMigration = 0.0;
    }
};

// =============================================================================
// BIAS ANALYSIS RESULT
// =============================================================================
struct BiasAnalysis {
    BiasType bias = BiasType::NONE;
    double strength = 0.0;       // 0.0 to 1.0
    std::string reason;
    
    void reset() {
        bias = BiasType::NONE;
        strength = 0.0;
        reason.clear();
    }
};

// =============================================================================
// SESSION HANDOFF PROFILE
// =============================================================================
class SessionHandoffProfile {
public:
    SessionHandoffProfile();
    
    // =========================================================================
    // MAIN TICK HANDLER
    // =========================================================================
    void onTick(const SHMarketSnapshot& snap);
    
    // =========================================================================
    // FILL HANDLER
    // =========================================================================
    void onFill(double fillPrice, double qty, bool isBuy);
    
    // =========================================================================
    // POSITION CLOSE HANDLER
    // =========================================================================
    void onPositionClosed(double pnl_bps, uint64_t heldNs);
    
    // =========================================================================
    // TIMER
    // =========================================================================
    void onTimer(uint64_t now_ns);
    
    // =========================================================================
    // DAY RESET
    // =========================================================================
    void resetDay();
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    const char* name() const { return "SESSION_HANDOFF"; }
    SHState state() const { return state_; }
    SHIdleReason idleReason() const { return idleReason_; }
    bool hasPosition() const { return hasPosition_; }
    int tradesThisDay() const { return tradesThisDay_; }
    BiasType currentBias() const { return biasAnalysis_.bias; }
    double biasStrength() const { return biasAnalysis_.strength; }
    
    // =========================================================================
    // STATUS OUTPUT
    // =========================================================================
    void printStatus() const;
    void toJSON(char* buf, size_t buf_size) const;
    
    // =========================================================================
    // ENABLE/DISABLE
    // =========================================================================
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; state_ = SHState::IDLE; }
    bool isEnabled() const { return enabled_; }

private:
    // State
    SHState state_ = SHState::IDLE;
    SHIdleReason idleReason_ = SHIdleReason::NONE;
    uint64_t stateTs_ns_ = 0;
    
    // Current handoff
    HandoffType currentHandoff_ = HandoffType::NONE;
    std::string observingSession_;
    std::string targetSession_;
    
    // Per-symbol analysis
    std::unordered_map<std::string, PriorSessionAnalysis> priorAnalysis_;
    BiasAnalysis biasAnalysis_;
    
    // Handoffs traded today
    std::unordered_set<std::string> tradedHandoffs_;  // "ASIA→LDN", "LDN→NY"
    
    // Position tracking
    bool hasPosition_ = false;
    SHSide positionSide_ = SHSide::NONE;
    double entryPrice_ = 0.0;
    double entryVwap_ = 0.0;
    uint64_t tradeStartNs_ = 0;
    std::string currentSymbol_;
    
    // Daily tracking
    int tradesThisDay_ = 0;
    
    // VWAP tracking
    VwapState vwapState_;
    
    // Session tracking
    std::string lastSession_;
    
    // Enable flag
    std::atomic<bool> enabled_{true};
    
    // =========================================================================
    // HANDOFF WINDOWS (UTC times)
    // =========================================================================
    // Asia→London: 06:45-07:15 UTC
    // London→NY: 13:15-13:45 UTC
    
    // =========================================================================
    // INTERNAL METHODS
    // =========================================================================
    HandoffType detectHandoffWindow(const SHMarketSnapshot& snap);
    void observePriorSession(const SHMarketSnapshot& snap);
    BiasAnalysis determineBias(const char* symbol);
    bool confirmBias(const SHMarketSnapshot& snap);
    void evaluateEntry(const SHMarketSnapshot& snap);
    void evaluateExit(const SHMarketSnapshot& snap);
    
    void enterTrade(SHSide side, const SHMarketSnapshot& snap);
    void exitTrade(const char* reason, const SHMarketSnapshot& snap);
    
    bool hardGatesPass(const SHMarketSnapshot& snap);
    
    // Output (virtual for override in actual implementation)
    virtual void submitOrder(SHSide side, double qty, const char* symbol) {
        printf("[SESSION_HO] SUBMIT %s %.6f %s\n", shSideToString(side), qty, symbol);
    }
    
    virtual void closePosition(const char* reason) {
        printf("[SESSION_HO] CLOSE: %s\n", reason);
    }
    
    // Real time function - uses steady_clock
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // Real sizing function - Session handoff uses larger size (0.20%)
    double calculateQty(double riskPct, const SHMarketSnapshot& snap) {
        double spreadPts = snap.spread();
        if (spreadPts <= 0) spreadPts = 0.0001;
        
        // Session handoff is structural - larger size, wider stops
        double riskAmount = 10000.0 * riskPct;
        double stopDistPts = spreadPts * 5.0;  // Wider stops for structural trades
        double pointValue = 1.0;
        
        if (strstr(snap.symbol, "NAS") || strstr(snap.symbol, "SPX")) {
            pointValue = 1.0;
        } else if (strstr(snap.symbol, "XAU")) {
            pointValue = 0.1;
        }
        
        double qty = riskAmount / (stopDistPts * pointValue * 100.0);
        return std::clamp(qty, 0.01, 10.0);  // Larger max for structural trades
    }
};

} // namespace Chimera
