// =============================================================================
// StopRunFadeProfile.hpp - v4.9.0 - STOP RUN FADE PROFILE
// =============================================================================
// 🎯 STOP RUN FADE — CORE PHILOSOPHY
//
// This profile monetizes STOP LIQUIDITY, not direction.
//
// When price:
//   - Accelerates fast (velocity spike)
//   - Sweeps liquidity (extreme imbalance)
//   - Fails to continue (no follow-through)
//
// The FAILURE is the edge.
//
// This is extremely consistent with very low drawdown because:
//   - We're not predicting direction
//   - We're fading liquidity grabs that already failed
//   - Invalidation is instant and tight
//
// =============================================================================
// STATE MACHINE:
//
// IDLE → RUN_DETECTED → CONFIRM_FAIL → IN_TRADE → COOLDOWN
//
// =============================================================================
// ENTRY LOGIC:
//
// 1. DETECT STOP RUN:
//    - Velocity spike > threshold
//    - Range expansion > 2× baseline
//    - Book imbalance extreme (> 0.85)
//
// 2. CONFIRM FAILURE:
//    - No continuation in 150ms
//    - VWAP rejects the move
//    - Imbalance collapses (< 0.4)
//
// 3. ENTRY:
//    - Enter AGAINST the run
//    - Small size, instant invalidation
//
// =============================================================================
// EXIT LOGIC:
//
// - Time cap: 3s
// - VWAP touch (profit)
// - Imbalance flip against (structure invalidated)
// - Latency degrade (safety)
//
// =============================================================================
// RISK MODEL (NON-NEGOTIABLE):
//   - Risk per trade: 0.05% – 0.10%
//   - Many trades per day (gated by cooldown)
//   - Symbols: Indices + Gold
//   - Sessions: NY, London (not Asia)
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
#include <deque>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Chimera {

// =============================================================================
// STOP RUN FADE STATE MACHINE
// =============================================================================
enum class SRState : uint8_t {
    IDLE = 0,           // Waiting for conditions
    RUN_DETECTED = 1,   // Stop run identified, waiting for failure
    CONFIRM_FAIL = 2,   // Failure confirmed, ready to enter
    IN_TRADE = 3,       // Position open
    COOLDOWN = 4        // Post-trade cooldown
};

inline const char* srStateToString(SRState s) {
    switch (s) {
        case SRState::IDLE:          return "IDLE";
        case SRState::RUN_DETECTED:  return "RUN_DETECTED";
        case SRState::CONFIRM_FAIL:  return "CONFIRM_FAIL";
        case SRState::IN_TRADE:      return "IN_TRADE";
        case SRState::COOLDOWN:      return "COOLDOWN";
        default:                     return "UNKNOWN";
    }
}

// =============================================================================
// STOP RUN IDLE REASON
// =============================================================================
enum class SRIdleReason : uint8_t {
    NONE = 0,
    GO_NO_GO_BLOCK,
    LATENCY_UNSTABLE,
    SHOCK_ACTIVE,
    SESSION_DISABLED,
    SYMBOL_DISABLED,
    NO_RUN_DETECTED,
    WAITING_FAILURE,
    RUN_CONTINUED,
    POSITION_OPEN,
    COOLDOWN_ACTIVE,
    DAILY_LIMIT_REACHED
};

inline const char* srIdleReasonToString(SRIdleReason r) {
    switch (r) {
        case SRIdleReason::NONE:               return "NONE";
        case SRIdleReason::GO_NO_GO_BLOCK:     return "GO_NO_GO_BLOCK";
        case SRIdleReason::LATENCY_UNSTABLE:   return "LATENCY_UNSTABLE";
        case SRIdleReason::SHOCK_ACTIVE:       return "SHOCK_ACTIVE";
        case SRIdleReason::SESSION_DISABLED:   return "SESSION_DISABLED";
        case SRIdleReason::SYMBOL_DISABLED:    return "SYMBOL_DISABLED";
        case SRIdleReason::NO_RUN_DETECTED:    return "NO_RUN_DETECTED";
        case SRIdleReason::WAITING_FAILURE:    return "WAITING_FAILURE";
        case SRIdleReason::RUN_CONTINUED:      return "RUN_CONTINUED";
        case SRIdleReason::POSITION_OPEN:      return "POSITION_OPEN";
        case SRIdleReason::COOLDOWN_ACTIVE:    return "COOLDOWN_ACTIVE";
        case SRIdleReason::DAILY_LIMIT_REACHED:return "DAILY_LIMIT_REACHED";
        default:                               return "UNKNOWN";
    }
}

inline const char* srIdleReasonIcon(SRIdleReason r) {
    switch (r) {
        case SRIdleReason::NONE:               return "✓";
        case SRIdleReason::GO_NO_GO_BLOCK:     return "🚫";
        case SRIdleReason::LATENCY_UNSTABLE:   return "⚡";
        case SRIdleReason::SHOCK_ACTIVE:       return "💥";
        case SRIdleReason::SESSION_DISABLED:   return "⏰";
        case SRIdleReason::SYMBOL_DISABLED:    return "🔒";
        case SRIdleReason::NO_RUN_DETECTED:    return "👁️";
        case SRIdleReason::WAITING_FAILURE:    return "⏳";
        case SRIdleReason::RUN_CONTINUED:      return "➡️";
        case SRIdleReason::POSITION_OPEN:      return "📈";
        case SRIdleReason::COOLDOWN_ACTIVE:    return "❄️";
        case SRIdleReason::DAILY_LIMIT_REACHED:return "🛑";
        default:                               return "?";
    }
}

// =============================================================================
// TRADE SIDE
// =============================================================================
enum class SRSide : uint8_t {
    NONE = 0,
    BUY = 1,
    SELL = 2
};

inline const char* srSideToString(SRSide s) {
    switch (s) {
        case SRSide::BUY:  return "BUY";
        case SRSide::SELL: return "SELL";
        default:           return "NONE";
    }
}

// =============================================================================
// STOP RUN SYMBOL CONFIG
// =============================================================================
struct SRSymbolConfig {
    bool enabled = false;
    double velocityThreshold = 0.0;      // Points per second for run detection
    double rangeExpansionMult = 2.0;     // Must be 2× baseline range
    double extremeImbalance = 0.85;      // Imbalance threshold for run
    double imbalanceCollapse = 0.40;     // Imbalance must collapse to this
    uint64_t failureWindowNs = 150'000'000ULL;  // 150ms for failure confirmation
    uint64_t maxHoldNs = 3'000'000'000ULL;      // 3 second time cap
    uint64_t cooldownNs = 500'000'000ULL;       // 500ms cooldown
    double pointValue = 1.0;
};

inline SRSymbolConfig getSRConfig(const char* symbol) {
    SRSymbolConfig cfg;
    
    if (strcmp(symbol, "NAS100") == 0) {
        cfg.enabled = true;
        cfg.velocityThreshold = 15.0;    // 15 points/sec
        cfg.rangeExpansionMult = 2.0;
        cfg.extremeImbalance = 0.85;
        cfg.imbalanceCollapse = 0.40;
        cfg.failureWindowNs = 150'000'000ULL;
        cfg.maxHoldNs = 3'000'000'000ULL;
        cfg.cooldownNs = 500'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "US30") == 0) {
        cfg.enabled = true;
        cfg.velocityThreshold = 25.0;    // 25 points/sec
        cfg.rangeExpansionMult = 2.0;
        cfg.extremeImbalance = 0.82;
        cfg.imbalanceCollapse = 0.38;
        cfg.failureWindowNs = 150'000'000ULL;
        cfg.maxHoldNs = 3'000'000'000ULL;
        cfg.cooldownNs = 500'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "SPX500") == 0) {
        cfg.enabled = true;
        cfg.velocityThreshold = 5.0;     // 5 points/sec
        cfg.rangeExpansionMult = 2.0;
        cfg.extremeImbalance = 0.88;
        cfg.imbalanceCollapse = 0.42;
        cfg.failureWindowNs = 120'000'000ULL;  // 120ms (faster)
        cfg.maxHoldNs = 2'500'000'000ULL;
        cfg.cooldownNs = 400'000'000ULL;
        cfg.pointValue = 1.0;
    }
    else if (strcmp(symbol, "XAUUSD") == 0) {
        cfg.enabled = true;
        cfg.velocityThreshold = 3.0;     // $3/sec
        cfg.rangeExpansionMult = 2.2;
        cfg.extremeImbalance = 0.80;
        cfg.imbalanceCollapse = 0.35;
        cfg.failureWindowNs = 180'000'000ULL;  // 180ms (gold is slower)
        cfg.maxHoldNs = 4'000'000'000ULL;
        cfg.cooldownNs = 600'000'000ULL;
        cfg.pointValue = 1.0;
    }
    
    return cfg;
}

inline bool isSRSymbolEnabled(const char* symbol) {
    return getSRConfig(symbol).enabled;
}

// =============================================================================
// SESSION POLICY FOR STOP RUN FADE
// =============================================================================
struct SRSessionPolicy {
    bool enabled = false;
    double riskMultiplier = 1.0;
    int maxTradesPerSession = 20;
    
    bool isEnabled() const { return enabled && riskMultiplier > 0.0; }
};

inline SRSessionPolicy getSRSessionPolicy(const std::string& session) {
    SRSessionPolicy policy;
    
    if (session == "NY_OPEN" || session == "NY" || session == "NY_MID") {
        policy.enabled = true;
        policy.riskMultiplier = 1.0;
        policy.maxTradesPerSession = 20;
    }
    else if (session == "LONDON" || session == "LDN" || session == "LONDON_NY") {
        policy.enabled = true;
        policy.riskMultiplier = 0.8;
        policy.maxTradesPerSession = 15;
    }
    else if (session == "ASIA") {
        // Disabled for Asia - not enough liquidity
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
// MARKET SNAPSHOT (INPUT TO STOP RUN FADE)
// =============================================================================
struct SRMarketSnapshot {
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
    double imbalance = 0.0;          // -1.0 to +1.0 (extreme = stop run)
    
    // Velocity (computed externally or here)
    double velocity = 0.0;           // Points per second
    
    // State flags
    bool latencyStable = false;
    bool shockActive = false;
    bool goNoGoIsGo = false;
    const char* currentSession = "";
    
    // Computed helpers
    double spread() const { return ask - bid; }
};

// =============================================================================
// STOP RUN STATE (per detection)
// =============================================================================
struct StopRunState {
    bool detected = false;
    int direction = 0;               // +1 = run up (shorts stopped), -1 = run down
    double runStartPrice = 0.0;
    double runPeakPrice = 0.0;
    uint64_t runStartNs = 0;
    double peakImbalance = 0.0;
    double baselineRange = 0.0;
    
    void reset() {
        detected = false;
        direction = 0;
        runStartPrice = 0.0;
        runPeakPrice = 0.0;
        runStartNs = 0;
        peakImbalance = 0.0;
        baselineRange = 0.0;
    }
};

// =============================================================================
// VELOCITY TRACKER (for run detection)
// =============================================================================
class VelocityTracker {
public:
    void addTick(double price, uint64_t ts_ns) {
        if (prices_.size() >= MAX_TICKS) {
            prices_.pop_front();
            timestamps_.pop_front();
        }
        prices_.push_back(price);
        timestamps_.push_back(ts_ns);
    }
    
    double getVelocity() const {
        if (prices_.size() < 2) return 0.0;
        
        double priceChange = prices_.back() - prices_.front();
        double timeSec = static_cast<double>(timestamps_.back() - timestamps_.front()) / 1e9;
        
        if (timeSec <= 0.0) return 0.0;
        return std::abs(priceChange) / timeSec;
    }
    
    double getRange() const {
        if (prices_.empty()) return 0.0;
        
        double high = prices_.front();
        double low = prices_.front();
        
        for (double p : prices_) {
            high = std::max(high, p);
            low = std::min(low, p);
        }
        
        return high - low;
    }
    
    void clear() {
        prices_.clear();
        timestamps_.clear();
    }
    
private:
    static constexpr size_t MAX_TICKS = 50;
    std::deque<double> prices_;
    std::deque<uint64_t> timestamps_;
};

// =============================================================================
// BASELINE RANGE TRACKER
// =============================================================================
class BaselineRangeTracker {
public:
    void update(double range) {
        const double alpha = 0.05;  // Slow EMA
        if (baseline_ <= 0.0) {
            baseline_ = range;
        } else {
            baseline_ = alpha * range + (1.0 - alpha) * baseline_;
        }
    }
    
    double get() const { return baseline_; }
    void reset() { baseline_ = 0.0; }
    
private:
    double baseline_ = 0.0;
};

// =============================================================================
// STOP RUN FADE PROFILE
// =============================================================================
class StopRunFadeProfile {
public:
    StopRunFadeProfile();
    
    // =========================================================================
    // MAIN TICK HANDLER
    // =========================================================================
    void onTick(const SRMarketSnapshot& snap);
    
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
    const char* name() const { return "STOP_RUN_FADE"; }
    SRState state() const { return state_; }
    SRIdleReason idleReason() const { return idleReason_; }
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
    void disable() { enabled_ = false; state_ = SRState::IDLE; }
    bool isEnabled() const { return enabled_; }

private:
    // State
    SRState state_ = SRState::IDLE;
    SRIdleReason idleReason_ = SRIdleReason::NONE;
    uint64_t stateTs_ns_ = 0;
    
    // Per-symbol tracking
    std::unordered_map<std::string, StopRunState> runs_;
    std::unordered_map<std::string, VelocityTracker> velocityTrackers_;
    std::unordered_map<std::string, BaselineRangeTracker> baselineTrackers_;
    
    // Position tracking
    bool hasPosition_ = false;
    SRSide positionSide_ = SRSide::NONE;
    double entryPrice_ = 0.0;
    double entryVwap_ = 0.0;
    uint64_t tradeStartNs_ = 0;
    std::string currentSymbol_;
    
    // Session tracking
    int tradesThisSession_ = 0;
    std::string currentSession_;
    SRSessionPolicy sessionPolicy_;
    
    // Loss tracking
    LossVelocity lossVelocity_;
    uint64_t lastTradeEndNs_ = 0;
    
    // VWAP tracking
    VwapState vwapState_;
    
    // Enable flag
    std::atomic<bool> enabled_{true};
    
    // =========================================================================
    // INTERNAL METHODS
    // =========================================================================
    bool detectStopRun(const SRMarketSnapshot& snap);
    bool confirmFailure(const SRMarketSnapshot& snap);
    void evaluateEntry(const SRMarketSnapshot& snap);
    void evaluateExit(const SRMarketSnapshot& snap);
    
    void enterTrade(SRSide side, const SRMarketSnapshot& snap);
    void exitTrade(const char* reason, const SRMarketSnapshot& snap);
    
    bool hardGatesPass(const SRMarketSnapshot& snap);
    
    // Output (virtual for override in actual implementation)
    virtual void submitOrder(SRSide side, double qty, const char* symbol) {
        printf("[STOP_RUN] SUBMIT %s %.6f %s\n", srSideToString(side), qty, symbol);
    }
    
    virtual void closePosition(const char* reason) {
        printf("[STOP_RUN] CLOSE: %s\n", reason);
    }
    
    // Real time function - uses steady_clock
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // Real sizing function - Stop run fade uses small size (0.05-0.10%)
    double calculateQty(double riskPct, const SRMarketSnapshot& snap) {
        double spreadPts = snap.spread();
        if (spreadPts <= 0) spreadPts = 0.0001;
        
        // Stop run fades are counter-trend - smaller size, tight stops
        double riskAmount = 10000.0 * riskPct;
        double stopDistPts = spreadPts * 2.0;  // Very tight stops
        double pointValue = 1.0;
        
        if (strstr(snap.symbol, "NAS") || strstr(snap.symbol, "SPX")) {
            pointValue = 1.0;
        } else if (strstr(snap.symbol, "XAU")) {
            pointValue = 0.1;
        }
        
        double qty = riskAmount / (stopDistPts * pointValue * 100.0);
        return std::clamp(qty, 0.01, 3.0);  // Small max for counter-trend
    }
};

} // namespace Chimera
