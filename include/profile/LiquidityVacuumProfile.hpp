// =============================================================================
// LiquidityVacuumProfile.hpp - v4.9.0 - LIQUIDITY VACUUM ENGINE
// =============================================================================
// 🎯 LIQUIDITY VACUUM — CORE PHILOSOPHY
//
// This engine monetizes occasional liquidity gaps where:
//   - Quotes pull (market makers step back)
//   - Depth thins dramatically
//   - Price jumps violently for 1-2 seconds
//
// These moves are MECHANICAL, not informational.
// You don't predict them — you confirm and latch on.
//
// =============================================================================
// STATE MACHINE:
//
// IDLE → VACUUM_DETECTED → CONFIRM_CONTINUATION → IN_TRADE → DONE
//
// One trade per event.
//
// =============================================================================
// ENTRY LOGIC:
//
// 1. VACUUM DETECTION:
//    - Bid/ask depth drops > 60%
//    - Price jumps ≥ X ticks in ≤ 120ms
//    - Spread does NOT widen abnormally (distinguishes from news)
//
// 2. CONFIRMATION (next 100ms):
//    - Continues in same direction
//    - VWAP slope aligns
//    → If confirmation fails → no trade
//
// =============================================================================
// EXIT LOGIC:
//   - Time cap: 1.0–1.5 seconds
//   - Continuation stalls
//   - VWAP rejection
//   - Latency spike
//   - This engine NEVER holds.
//
// =============================================================================
// RISK MODEL (NON-NEGOTIABLE):
//   - Risk per trade: 0.05%
//   - Frequency: low–moderate (event-driven)
//   - Win rate: medium
//   - Payoff: asymmetric (fast spikes)
//   - Symbols: NAS100, US30, XAUUSD, SPX500
//   - Sessions: NY or London
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "micro/VwapAcceleration.hpp"
#include "risk/LossVelocity.hpp"

#include <string>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <unordered_map>
#include <deque>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Chimera {

// =============================================================================
// LIQUIDITY VACUUM STATE MACHINE
// =============================================================================
enum class LVState : uint8_t {
    IDLE = 0,                 // Waiting for vacuum event
    VACUUM_DETECTED = 1,      // Vacuum identified, watching for continuation
    CONFIRM_CONTINUATION = 2, // Continuation confirmed, ready to enter
    IN_TRADE = 3,             // Position open
    DONE = 4                  // Event complete, cooldown
};

inline const char* lvStateToString(LVState s) {
    switch (s) {
        case LVState::IDLE:                 return "IDLE";
        case LVState::VACUUM_DETECTED:      return "VACUUM_DETECTED";
        case LVState::CONFIRM_CONTINUATION: return "CONFIRM_CONTINUATION";
        case LVState::IN_TRADE:             return "IN_TRADE";
        case LVState::DONE:                 return "DONE";
        default:                            return "UNKNOWN";
    }
}

// =============================================================================
// LIQUIDITY VACUUM IDLE REASON
// =============================================================================
enum class LVIdleReason : uint8_t {
    NONE = 0,
    GO_NO_GO_BLOCK,
    LATENCY_UNSTABLE,
    SHOCK_ACTIVE,
    SESSION_DISABLED,
    SYMBOL_DISABLED,
    NO_VACUUM_DETECTED,
    WAITING_CONFIRMATION,
    CONFIRMATION_FAILED,
    SPREAD_TOO_WIDE,
    POSITION_OPEN,
    COOLDOWN_ACTIVE,
    TRADE_LIMIT_REACHED
};

inline const char* lvIdleReasonToString(LVIdleReason r) {
    switch (r) {
        case LVIdleReason::NONE:                 return "NONE";
        case LVIdleReason::GO_NO_GO_BLOCK:       return "GO_NO_GO_BLOCK";
        case LVIdleReason::LATENCY_UNSTABLE:     return "LATENCY_UNSTABLE";
        case LVIdleReason::SHOCK_ACTIVE:         return "SHOCK_ACTIVE";
        case LVIdleReason::SESSION_DISABLED:     return "SESSION_DISABLED";
        case LVIdleReason::SYMBOL_DISABLED:      return "SYMBOL_DISABLED";
        case LVIdleReason::NO_VACUUM_DETECTED:   return "NO_VACUUM_DETECTED";
        case LVIdleReason::WAITING_CONFIRMATION: return "WAITING_CONFIRMATION";
        case LVIdleReason::CONFIRMATION_FAILED:  return "CONFIRMATION_FAILED";
        case LVIdleReason::SPREAD_TOO_WIDE:      return "SPREAD_TOO_WIDE";
        case LVIdleReason::POSITION_OPEN:        return "POSITION_OPEN";
        case LVIdleReason::COOLDOWN_ACTIVE:      return "COOLDOWN_ACTIVE";
        case LVIdleReason::TRADE_LIMIT_REACHED:  return "TRADE_LIMIT_REACHED";
        default:                                 return "UNKNOWN";
    }
}

inline const char* lvIdleReasonIcon(LVIdleReason r) {
    switch (r) {
        case LVIdleReason::NONE:                 return "✓";
        case LVIdleReason::GO_NO_GO_BLOCK:       return "🚫";
        case LVIdleReason::LATENCY_UNSTABLE:     return "⚡";
        case LVIdleReason::SHOCK_ACTIVE:         return "💥";
        case LVIdleReason::SESSION_DISABLED:     return "⏰";
        case LVIdleReason::SYMBOL_DISABLED:      return "🔒";
        case LVIdleReason::NO_VACUUM_DETECTED:   return "👁️";
        case LVIdleReason::WAITING_CONFIRMATION: return "⏳";
        case LVIdleReason::CONFIRMATION_FAILED:  return "❌";
        case LVIdleReason::SPREAD_TOO_WIDE:      return "📏";
        case LVIdleReason::POSITION_OPEN:        return "📈";
        case LVIdleReason::COOLDOWN_ACTIVE:      return "❄️";
        case LVIdleReason::TRADE_LIMIT_REACHED:  return "🛑";
        default:                                 return "?";
    }
}

// =============================================================================
// TRADE SIDE
// =============================================================================
enum class LVSide : uint8_t {
    NONE = 0,
    BUY = 1,
    SELL = 2
};

inline const char* lvSideToString(LVSide s) {
    switch (s) {
        case LVSide::BUY:  return "BUY";
        case LVSide::SELL: return "SELL";
        default:           return "NONE";
    }
}

// =============================================================================
// LIQUIDITY VACUUM SYMBOL CONFIG
// =============================================================================
struct LVSymbolConfig {
    bool enabled = false;
    double depthDropPct = 0.60;           // Depth must drop > 60%
    int minJumpTicks = 3;                 // Minimum tick jump
    uint64_t jumpWindowNs = 120'000'000ULL;   // 120ms for jump
    uint64_t confirmWindowNs = 100'000'000ULL; // 100ms for confirmation
    double maxSpreadMult = 2.5;           // Spread must not widen > 2.5× normal
    uint64_t maxHoldNs = 1'500'000'000ULL;    // 1.5s time cap
    uint64_t cooldownNs = 1'000'000'000ULL;   // 1s cooldown
    double tickSize = 0.25;               // For tick calculation
};

inline LVSymbolConfig getLVConfig(const char* symbol) {
    LVSymbolConfig cfg;
    
    if (strcmp(symbol, "NAS100") == 0) {
        cfg.enabled = true;
        cfg.depthDropPct = 0.60;
        cfg.minJumpTicks = 4;
        cfg.jumpWindowNs = 120'000'000ULL;
        cfg.confirmWindowNs = 100'000'000ULL;
        cfg.maxSpreadMult = 2.5;
        cfg.maxHoldNs = 1'500'000'000ULL;
        cfg.cooldownNs = 1'000'000'000ULL;
        cfg.tickSize = 0.25;
    }
    else if (strcmp(symbol, "US30") == 0) {
        cfg.enabled = true;
        cfg.depthDropPct = 0.55;
        cfg.minJumpTicks = 3;
        cfg.jumpWindowNs = 130'000'000ULL;
        cfg.confirmWindowNs = 110'000'000ULL;
        cfg.maxSpreadMult = 2.8;
        cfg.maxHoldNs = 1'500'000'000ULL;
        cfg.cooldownNs = 1'000'000'000ULL;
        cfg.tickSize = 1.0;
    }
    else if (strcmp(symbol, "SPX500") == 0) {
        cfg.enabled = true;
        cfg.depthDropPct = 0.65;
        cfg.minJumpTicks = 3;
        cfg.jumpWindowNs = 100'000'000ULL;
        cfg.confirmWindowNs = 80'000'000ULL;
        cfg.maxSpreadMult = 2.2;
        cfg.maxHoldNs = 1'200'000'000ULL;
        cfg.cooldownNs = 800'000'000ULL;
        cfg.tickSize = 0.25;
    }
    else if (strcmp(symbol, "XAUUSD") == 0) {
        cfg.enabled = true;
        cfg.depthDropPct = 0.55;
        cfg.minJumpTicks = 5;
        cfg.jumpWindowNs = 150'000'000ULL;
        cfg.confirmWindowNs = 120'000'000ULL;
        cfg.maxSpreadMult = 3.0;
        cfg.maxHoldNs = 2'000'000'000ULL;
        cfg.cooldownNs = 1'200'000'000ULL;
        cfg.tickSize = 0.01;
    }
    
    return cfg;
}

inline bool isLVSymbolEnabled(const char* symbol) {
    return getLVConfig(symbol).enabled;
}

// =============================================================================
// SESSION POLICY FOR LIQUIDITY VACUUM
// =============================================================================
struct LVSessionPolicy {
    bool enabled = false;
    double riskMultiplier = 1.0;
    int maxTradesPerSession = 10;
    
    bool isEnabled() const { return enabled && riskMultiplier > 0.0; }
};

inline LVSessionPolicy getLVSessionPolicy(const std::string& session) {
    LVSessionPolicy policy;
    
    if (session == "NY_OPEN" || session == "NY" || session == "NY_MID") {
        policy.enabled = true;
        policy.riskMultiplier = 1.0;
        policy.maxTradesPerSession = 10;
    }
    else if (session == "LONDON" || session == "LDN" || session == "LONDON_NY") {
        policy.enabled = true;
        policy.riskMultiplier = 0.8;
        policy.maxTradesPerSession = 8;
    }
    else if (session == "ASIA") {
        // Disabled for Asia
        policy.enabled = false;
        policy.riskMultiplier = 0.0;
        policy.maxTradesPerSession = 0;
    }
    else {
        policy.enabled = false;
        policy.riskMultiplier = 0.0;
        policy.maxTradesPerSession = 0;
    }
    
    return policy;
}

// =============================================================================
// MARKET SNAPSHOT (INPUT TO LIQUIDITY VACUUM)
// =============================================================================
struct LVMarketSnapshot {
    // Identification
    const char* symbol = "";
    uint64_t now_ns = 0;
    
    // Price data
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    double vwap = 0.0;
    double vwapSlope = 0.0;
    
    // Depth data
    double bidDepth = 0.0;       // Total bid depth
    double askDepth = 0.0;       // Total ask depth
    double baselineBidDepth = 0.0;  // EMA baseline
    double baselineAskDepth = 0.0;
    double baselineSpread = 0.0;    // Normal spread
    
    // State flags
    bool latencyStable = false;
    bool shockActive = false;
    bool goNoGoIsGo = false;
    const char* currentSession = "";
    
    // Computed helpers
    double spread() const { return ask - bid; }
    double totalDepth() const { return bidDepth + askDepth; }
    double baselineTotalDepth() const { return baselineBidDepth + baselineAskDepth; }
    double depthRatio() const { 
        return baselineTotalDepth() > 0 ? totalDepth() / baselineTotalDepth() : 1.0;
    }
    double spreadRatio() const {
        return baselineSpread > 0 ? spread() / baselineSpread : 1.0;
    }
};

// =============================================================================
// VACUUM STATE (per event)
// =============================================================================
struct VacuumState {
    bool detected = false;
    int direction = 0;           // +1 = up vacuum, -1 = down vacuum
    double jumpStartPrice = 0.0;
    double jumpPeakPrice = 0.0;
    uint64_t jumpStartNs = 0;
    double depthAtJump = 0.0;
    double spreadAtJump = 0.0;
    int ticksMoved = 0;
    bool confirmationStarted = false;
    uint64_t confirmStartNs = 0;
    
    void reset() {
        detected = false;
        direction = 0;
        jumpStartPrice = 0.0;
        jumpPeakPrice = 0.0;
        jumpStartNs = 0;
        depthAtJump = 0.0;
        spreadAtJump = 0.0;
        ticksMoved = 0;
        confirmationStarted = false;
        confirmStartNs = 0;
    }
};

// =============================================================================
// PRICE HISTORY FOR JUMP DETECTION
// =============================================================================
class PriceJumpTracker {
public:
    void addTick(double price, uint64_t ts_ns) {
        if (prices_.size() >= MAX_TICKS) {
            prices_.pop_front();
            timestamps_.pop_front();
        }
        prices_.push_back(price);
        timestamps_.push_back(ts_ns);
    }
    
    // Get price change in last N nanoseconds
    double getPriceChange(uint64_t windowNs, uint64_t now_ns) const {
        if (prices_.size() < 2) return 0.0;
        
        double endPrice = prices_.back();
        double startPrice = prices_.back();
        
        // Find price at start of window
        for (size_t i = 0; i < prices_.size(); ++i) {
            if (now_ns - timestamps_[i] <= windowNs) {
                startPrice = prices_[i];
                break;
            }
        }
        
        return endPrice - startPrice;
    }
    
    int getTicksMoved(uint64_t windowNs, uint64_t now_ns, double tickSize) const {
        double change = getPriceChange(windowNs, now_ns);
        return static_cast<int>(std::abs(change) / tickSize);
    }
    
    double getStartPrice(uint64_t windowNs, uint64_t now_ns) const {
        for (size_t i = 0; i < timestamps_.size(); ++i) {
            if (now_ns - timestamps_[i] <= windowNs) {
                return prices_[i];
            }
        }
        return prices_.empty() ? 0.0 : prices_.front();
    }
    
    void clear() {
        prices_.clear();
        timestamps_.clear();
    }
    
private:
    static constexpr size_t MAX_TICKS = 100;
    std::deque<double> prices_;
    std::deque<uint64_t> timestamps_;
};

// =============================================================================
// LIQUIDITY VACUUM PROFILE
// =============================================================================
class LiquidityVacuumProfile {
public:
    LiquidityVacuumProfile();
    
    // =========================================================================
    // MAIN TICK HANDLER
    // =========================================================================
    void onTick(const LVMarketSnapshot& snap);
    
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
    // SESSION RESET
    // =========================================================================
    void resetSession();
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    const char* name() const { return "LIQUIDITY_VACUUM"; }
    LVState state() const { return state_; }
    LVIdleReason idleReason() const { return idleReason_; }
    bool hasPosition() const { return hasPosition_; }
    int tradesThisSession() const { return tradesThisSession_; }
    
    // =========================================================================
    // STATUS OUTPUT
    // =========================================================================
    void printStatus() const;
    void toJSON(char* buf, size_t buf_size) const;
    
    // =========================================================================
    // ENABLE/DISABLE
    // =========================================================================
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; state_ = LVState::IDLE; }
    bool isEnabled() const { return enabled_; }

private:
    // State
    LVState state_ = LVState::IDLE;
    LVIdleReason idleReason_ = LVIdleReason::NONE;
    uint64_t stateTs_ns_ = 0;
    
    // Per-symbol tracking
    std::unordered_map<std::string, VacuumState> vacuums_;
    std::unordered_map<std::string, PriceJumpTracker> jumpTrackers_;
    
    // Position tracking
    bool hasPosition_ = false;
    LVSide positionSide_ = LVSide::NONE;
    double entryPrice_ = 0.0;
    double peakPrice_ = 0.0;  // Track best price achieved
    uint64_t tradeStartNs_ = 0;
    std::string currentSymbol_;
    
    // Session tracking
    int tradesThisSession_ = 0;
    std::string currentSession_;
    LVSessionPolicy sessionPolicy_;
    
    // VWAP tracking
    VwapState vwapState_;
    
    // Enable flag
    std::atomic<bool> enabled_{true};
    
    // =========================================================================
    // INTERNAL METHODS
    // =========================================================================
    bool detectVacuum(const LVMarketSnapshot& snap);
    bool confirmContinuation(const LVMarketSnapshot& snap);
    void evaluateEntry(const LVMarketSnapshot& snap);
    void evaluateExit(const LVMarketSnapshot& snap);
    
    void enterTrade(LVSide side, const LVMarketSnapshot& snap);
    void exitTrade(const char* reason, const LVMarketSnapshot& snap);
    
    bool hardGatesPass(const LVMarketSnapshot& snap);
    
    // Output (virtual for override in actual implementation)
    virtual void submitOrder(LVSide side, double qty, const char* symbol) {
        printf("[LIQ_VAC] SUBMIT %s %.6f %s\n", lvSideToString(side), qty, symbol);
    }
    
    virtual void closePosition(const char* reason) {
        printf("[LIQ_VAC] CLOSE: %s\n", reason);
    }
    
    // Real time function - uses steady_clock
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // Real sizing function - Liquidity vacuum uses small size (0.05%)
    double calculateQty(double riskPct, const LVMarketSnapshot& snap) {
        double spreadPts = snap.spread();
        if (spreadPts <= 0) spreadPts = 0.0001;
        
        // Vacuum trades are fast and risky - small size, tight stops
        double riskAmount = 10000.0 * riskPct;
        double stopDistPts = spreadPts * 1.5;  // Very tight - vacuum or nothing
        double pointValue = 1.0;
        
        if (strstr(snap.symbol, "NAS") || strstr(snap.symbol, "SPX")) {
            pointValue = 1.0;
        } else if (strstr(snap.symbol, "XAU")) {
            pointValue = 0.1;
        }
        
        double qty = riskAmount / (stopDistPts * pointValue * 100.0);
        return std::clamp(qty, 0.01, 2.0);  // Small max for vacuum trades
    }
};

} // namespace Chimera
