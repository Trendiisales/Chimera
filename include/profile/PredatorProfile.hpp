// =============================================================================
// PredatorProfile.hpp - v4.8.0 - PREDATOR ULTRA-FAST SCALPING PROFILE
// =============================================================================
// 🧠 PREDATOR — CORE PHILOSOPHY
//
// Predator does NOT predict.
// It reacts faster than the market can lie.
//
// It only trades when:
//   - Structure is resolving
//   - Latency is clean
//   - Microstructure confirms immediately
//   - Invalidation is extremely tight
//
// If conditions are not perfect → it does nothing.
//
// =============================================================================
// ENTRY TYPES:
//
// TYPE A — IMBALANCE SNAPBACK (Fade Failure)
//   - OrderBookImbalance ≥ 0.75
//   - Price fails to continue within 120ms
//   - Book refills ≥ 65% inside 200ms
//   - VWAP slope flattens or reverses
//   → Enter against the failed imbalance
//
// TYPE B — MICRO BREAK + IMMEDIATE ACCEPTANCE
//   - Micro range break (last 500–800ms)
//   - Follow-through ≥ 2 ticks within 150ms
//   - No VWAP rejection
//   → Enter with the break
//
// =============================================================================
// RISK MODEL (NON-NEGOTIABLE):
//   - Risk per trade: 0.05% – 0.10%
//   - Max positions: 1
//   - No scaling, No averaging
//   - Max hold: 1.5s – 2.5s (symbol dependent)
//   - Losers are tiny. Winners are fast.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "PredatorSymbolConfig.hpp"
#include "PredatorSessionPolicy.hpp"
#include "PredatorIdleReason.hpp"
#include "micro/VwapAcceleration.hpp"
#include "risk/LossVelocity.hpp"
#include "audit/PredatorExpectancy.hpp"

#include <string>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Chimera {

// =============================================================================
// PREDATOR STATE MACHINE
// =============================================================================
enum class PredatorState : uint8_t {
    IDLE = 0,       // Waiting for all gates
    ARMED = 1,      // Microstructure window open
    IN_TRADE = 2,   // One position only
    COOLDOWN = 3    // Short forced pause
};

inline const char* predatorStateToString(PredatorState s) {
    switch (s) {
        case PredatorState::IDLE:     return "IDLE";
        case PredatorState::ARMED:    return "ARMED";
        case PredatorState::IN_TRADE: return "IN_TRADE";
        case PredatorState::COOLDOWN: return "COOLDOWN";
        default:                      return "UNKNOWN";
    }
}

// =============================================================================
// TRADE SIDE
// =============================================================================
enum class Side : uint8_t {
    NONE = 0,
    BUY = 1,
    SELL = 2
};

inline const char* sideToString(Side s) {
    switch (s) {
        case Side::BUY:  return "BUY";
        case Side::SELL: return "SELL";
        default:         return "NONE";
    }
}

// =============================================================================
// MARKET SNAPSHOT (INPUT TO PREDATOR)
// =============================================================================
struct PredatorMarketSnapshot {
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
    double bookRefillRatio = 0.0;    // 0.0 to 1.0
    
    // Microstructure
    double currentEdge = 0.0;
    double entryEdge = 0.0;
    bool imbalanceFlipped = false;
    bool vwapReclaimDetected = false;
    
    // Range break detection
    bool microRangeBreak = false;
    int breakDirection = 0;          // +1 = up, -1 = down
    int followThroughTicks = 0;
    uint64_t breakAge_ns = 0;
    
    // State
    bool latencyStable = false;
    bool structureResolving = false;
    bool regimeToxic = false;
    bool shockActive = false;
    bool goNoGoIsGo = false;
    const char* currentSession = "";
    
    // Computed helpers
    double spread() const { return ask - bid; }
    double edgeDecay() const { 
        return (entryEdge > 0) ? 1.0 - (currentEdge / entryEdge) : 0.0;
    }
    bool latencyDegraded() const { return !latencyStable; }
};

// =============================================================================
// PREDATOR PROFILE
// =============================================================================
class PredatorProfile {
public:
    PredatorProfile();
    
    // =========================================================================
    // MAIN TICK HANDLER
    // =========================================================================
    void onTick(const PredatorMarketSnapshot& snap);
    
    // =========================================================================
    // FILL HANDLER (call when order fills)
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
    // GETTERS
    // =========================================================================
    const char* name() const { return "PREDATOR"; }
    PredatorState state() const { return state_; }
    PredatorIdleReason idleReason() const { return idleReason_; }
    bool hasPosition() const { return hasPosition_; }
    int tradesThisSession() const { return tradesThisSession_; }
    
    // =========================================================================
    // STATUS OUTPUT
    // =========================================================================
    void printStatus() const;
    void toJSON(char* buf, size_t buf_size) const;
    
    // =========================================================================
    // RESET (new session)
    // =========================================================================
    void resetSession();
    
    // =========================================================================
    // ENABLE/DISABLE
    // =========================================================================
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; state_ = PredatorState::IDLE; }
    bool isEnabled() const { return enabled_; }

private:
    // State
    PredatorState state_ = PredatorState::IDLE;
    PredatorIdleReason idleReason_ = PredatorIdleReason::NONE;
    uint64_t stateTs_ns_ = 0;
    
    // Position tracking
    bool hasPosition_ = false;
    Side positionSide_ = Side::NONE;
    double entryPrice_ = 0.0;
    double entryEdge_ = 0.0;
    uint64_t tradeStartNs_ = 0;
    std::string currentSymbol_;
    
    // Session tracking
    int tradesThisSession_ = 0;
    std::string currentSession_;
    PredatorSessionPolicy sessionPolicy_;
    
    // Risk tracking
    LossVelocity lossVelocity_;
    ConsecutiveLossTracker consecutiveLosses_;
    uint64_t lastTradeEndNs_ = 0;
    
    // VWAP tracking
    VwapAccelState vwapAccelState_;
    
    // Enable flag
    std::atomic<bool> enabled_{true};
    
    // =========================================================================
    // INTERNAL METHODS
    // =========================================================================
    void evaluateEntry(const PredatorMarketSnapshot& snap);
    void evaluateExit(const PredatorMarketSnapshot& snap);
    
    bool checkImbalanceSnapback(const PredatorMarketSnapshot& snap) const;
    bool checkMicroBreakAcceptance(const PredatorMarketSnapshot& snap) const;
    
    void enterTrade(Side side, const PredatorMarketSnapshot& snap);
    void exitTrade(const char* reason, const PredatorMarketSnapshot& snap);
    
    bool hardGatesPass(const PredatorMarketSnapshot& snap);
    
    // Output (virtual for override in actual implementation)
    virtual void submitOrder(Side side, double qty, const char* symbol) {
        printf("[PREDATOR] SUBMIT %s %.6f %s\n", 
               sideToString(side), qty, symbol);
    }
    
    virtual void closePosition(const char* reason) {
        printf("[PREDATOR] CLOSE: %s\n", reason);
    }
    
    // Real time function - uses steady_clock
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    // Real sizing function - proper risk-based calculation
    double calculateQty(double riskPct, const PredatorMarketSnapshot& snap) {
        // Risk-based position sizing:
        // qty = (account_risk_amount) / (distance_to_stop * point_value)
        // Simplified: use spread as proxy for minimum move
        double spreadPts = snap.spread();
        if (spreadPts <= 0) spreadPts = 0.0001;  // Minimum
        
        // Assume notional $10k per 0.01% risk for CFD (engine should override)
        double riskAmount = 10000.0 * riskPct;  // e.g. $1 for 0.01%
        double stopDistPts = spreadPts * 3.0;   // 3x spread as stop
        
        // qty = risk_amount / (stop_dist * point_value)
        // For indices: point_value ~ $1 per point per 0.01 lots
        double pointValue = 1.0;
        if (strstr(snap.symbol, "NAS") || strstr(snap.symbol, "SPX")) {
            pointValue = 1.0;
        } else if (strstr(snap.symbol, "XAU")) {
            pointValue = 0.1;  // Gold has different point value
        }
        
        double qty = riskAmount / (stopDistPts * pointValue * 100.0);
        
        // Clamp to reasonable bounds
        return std::clamp(qty, 0.01, 10.0);
    }
};

} // namespace Chimera
