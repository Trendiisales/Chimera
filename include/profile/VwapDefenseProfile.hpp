// =============================================================================
// VwapDefenseProfile.hpp - v4.18.0 - VWAP DEFENSE / RECLAIM ENGINE
// =============================================================================
// 🎯 VWAP DEFENSE — CORE PHILOSOPHY
//
// Institutions defend VWAP aggressively when:
//   - Inventory is balanced
//   - Session participation is high
//   - Price briefly violates VWAP without follow-through
//
// Retail sees chop.
// Professionals see inventory defense.
//
// This engine does NOT trend trade.
// It trades failed VWAP breaks and snap reclaims.
//
// =============================================================================
// v4.18.0 CHANGES:
//   - VwapCalculator is now the SINGLE SOURCE OF TRUTH for VWAP
//   - VWAP no longer depends on upstream snap.vwap / snap.vwapSlope
//   - Added: onTrade() feeds VwapCalculator directly
//   - Added: VwapTune for symbol-specific tuning
//   - Added: VwapEdgeLogic for clean reclaim vs fail-fade classification
//   - Added: VwapPyramidRules (reclaim scales, fail-fade single-shot)
//   - Added: VwapPositionSizer (distance + slope confidence sizing)
//   - Added: SessionClock for session-aware gating
//   - Existing state machine, gates, session policies UNCHANGED
//
// =============================================================================
// STATE MACHINE:
//
// IDLE → VWAP_TESTING → RECLAIM_CONFIRMED → IN_TRADE → COOLDOWN
//
// Only one position per symbol.
//
// =============================================================================
// ENTRY TYPES:
//
// VARIANT A — VWAP RECLAIM (best case)
//   - Price below VWAP
//   - Pushes back above VWAP
//   - Holds for ≥ 300ms
//   - Imbalance flips supportive
//   - VWAP slope flattens or rises
//   → Entry: join in direction of reclaim
//   → Target: micro continuation
//   → Invalidation: VWAP lost again
//   → Pyramid: up to 3 adds if slope holds
//
// VARIANT B — VWAP FAIL FADE
//   - Price pushes above VWAP
//   - Fails within 400ms
//   - Imbalance collapses
//   - Price snaps back below VWAP
//   → Entry: fade the failed break
//   → Invalidation: sustained VWAP acceptance
//   → Pyramid: single-shot only
//
// =============================================================================
// EXIT LOGIC (BRUTAL, PROTECTIVE):
//   - Time cap: 5–8 seconds
//   - VWAP reclaimed against
//   - Edge decay > 45%
//   - Latency degradation
//   - Loss velocity toxic → cut early
//   - Losses are tiny, winners are clean.
//
// =============================================================================
// RISK MODEL (NON-NEGOTIABLE):
//   - Risk per trade: 0.05% – 0.10%
//   - Trades per symbol: moderate
//   - Win rate: high
//   - Reward:risk: ~1.2–1.8
//   - Symbols: NAS100, US30, SPX500, XAUUSD
//   - Sessions: NY or London (no Asia)
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include "micro/VwapAcceleration.hpp"
#include "micro/VwapCalculator.hpp"
#include "profile/VwapTune.hpp"
#include "edge/VwapEdgeLogic.hpp"
#include "edge/VwapPyramidRules.hpp"
#include "session/SessionClock.hpp"
#include "risk/LossVelocity.hpp"
#include "sizing/VwapPositionSizer.hpp"

#include <string>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Chimera {

// =============================================================================
// VWAP DEFENSE STATE MACHINE
// =============================================================================
enum class VDState : uint8_t {
    IDLE = 0,               // Waiting for VWAP test
    VWAP_TESTING = 1,       // Price testing VWAP, watching for break/reclaim
    RECLAIM_CONFIRMED = 2,  // Reclaim or fail confirmed, ready to enter
    IN_TRADE = 3,           // Position open
    COOLDOWN = 4            // Post-trade cooldown
};

inline const char* vdStateToString(VDState s) {
    switch (s) {
        case VDState::IDLE:              return "IDLE";
        case VDState::VWAP_TESTING:      return "VWAP_TESTING";
        case VDState::RECLAIM_CONFIRMED: return "RECLAIM_CONFIRMED";
        case VDState::IN_TRADE:          return "IN_TRADE";
        case VDState::COOLDOWN:          return "COOLDOWN";
        default:                         return "UNKNOWN";
    }
}

// =============================================================================
// VWAP DEFENSE IDLE REASON
// =============================================================================
enum class VDIdleReason : uint8_t {
    NONE = 0,
    GO_NO_GO_BLOCK,
    LATENCY_UNSTABLE,
    SHOCK_ACTIVE,
    SESSION_DISABLED,
    SYMBOL_DISABLED,
    REGIME_TOXIC,
    STRUCTURE_NOT_READY,
    NO_VWAP_TEST,
    WAITING_RECLAIM,
    WAITING_FAIL,
    POSITION_OPEN,
    COOLDOWN_ACTIVE,
    TRADE_LIMIT_REACHED,
    VWAP_NOT_READY
};

inline const char* vdIdleReasonToString(VDIdleReason r) {
    switch (r) {
        case VDIdleReason::NONE:               return "NONE";
        case VDIdleReason::GO_NO_GO_BLOCK:     return "GO_NO_GO_BLOCK";
        case VDIdleReason::LATENCY_UNSTABLE:   return "LATENCY_UNSTABLE";
        case VDIdleReason::SHOCK_ACTIVE:       return "SHOCK_ACTIVE";
        case VDIdleReason::SESSION_DISABLED:   return "SESSION_DISABLED";
        case VDIdleReason::SYMBOL_DISABLED:    return "SYMBOL_DISABLED";
        case VDIdleReason::REGIME_TOXIC:       return "REGIME_TOXIC";
        case VDIdleReason::STRUCTURE_NOT_READY:return "STRUCTURE_NOT_READY";
        case VDIdleReason::NO_VWAP_TEST:       return "NO_VWAP_TEST";
        case VDIdleReason::WAITING_RECLAIM:    return "WAITING_RECLAIM";
        case VDIdleReason::WAITING_FAIL:       return "WAITING_FAIL";
        case VDIdleReason::POSITION_OPEN:      return "POSITION_OPEN";
        case VDIdleReason::COOLDOWN_ACTIVE:    return "COOLDOWN_ACTIVE";
        case VDIdleReason::TRADE_LIMIT_REACHED:return "TRADE_LIMIT_REACHED";
        case VDIdleReason::VWAP_NOT_READY:     return "VWAP_NOT_READY";
        default:                               return "UNKNOWN";
    }
}

inline const char* vdIdleReasonIcon(VDIdleReason r) {
    switch (r) {
        case VDIdleReason::NONE:               return "✓";
        case VDIdleReason::GO_NO_GO_BLOCK:     return "🚫";
        case VDIdleReason::LATENCY_UNSTABLE:   return "⚡";
        case VDIdleReason::SHOCK_ACTIVE:       return "💥";
        case VDIdleReason::SESSION_DISABLED:   return "⏰";
        case VDIdleReason::SYMBOL_DISABLED:    return "🔒";
        case VDIdleReason::REGIME_TOXIC:       return "☠️";
        case VDIdleReason::STRUCTURE_NOT_READY:return "🔧";
        case VDIdleReason::NO_VWAP_TEST:       return "📊";
        case VDIdleReason::WAITING_RECLAIM:    return "⏳";
        case VDIdleReason::WAITING_FAIL:       return "↩️";
        case VDIdleReason::POSITION_OPEN:      return "📈";
        case VDIdleReason::COOLDOWN_ACTIVE:    return "❄️";
        case VDIdleReason::TRADE_LIMIT_REACHED:return "🛑";
        case VDIdleReason::VWAP_NOT_READY:     return "⏳";
        default:                               return "?";
    }
}

// =============================================================================
// TRADE SIDE
// =============================================================================
enum class VDSide : uint8_t {
    NONE = 0,
    BUY = 1,
    SELL = 2
};

inline const char* vdSideToString(VDSide s) {
    switch (s) {
        case VDSide::BUY:  return "BUY";
        case VDSide::SELL: return "SELL";
        default:           return "NONE";
    }
}

// =============================================================================
// ENTRY TYPE
// =============================================================================
enum class VDEntryType : uint8_t {
    NONE = 0,
    RECLAIM = 1,    // Variant A - VWAP Reclaim
    FAIL_FADE = 2   // Variant B - VWAP Fail Fade
};

inline const char* vdEntryTypeToString(VDEntryType t) {
    switch (t) {
        case VDEntryType::RECLAIM:   return "RECLAIM";
        case VDEntryType::FAIL_FADE: return "FAIL_FADE";
        default:                     return "NONE";
    }
}

// =============================================================================
// VWAP DEFENSE SYMBOL CONFIG (legacy — kept for backward compat)
// =============================================================================
struct VDSymbolConfig {
    bool enabled = false;
    double vwapProximityPct = 0.0015;
    uint64_t reclaimHoldNs = 300'000'000ULL;
    uint64_t failWindowNs = 400'000'000ULL;
    double supportiveImbalance = 0.40;
    double collapseImbalance = 0.25;
    double edgeDecayExit = 0.45;
    uint64_t maxHoldNs = 6'000'000'000ULL;
    uint64_t cooldownNs = 300'000'000ULL;
};

inline VDSymbolConfig getVDConfig(const char* symbol) {
    VDSymbolConfig cfg;

    if (strcmp(symbol, "NAS100") == 0) {
        cfg.enabled = true;
        cfg.vwapProximityPct = 0.0012;
        cfg.reclaimHoldNs = 300'000'000ULL;
        cfg.maxHoldNs = 6'000'000'000ULL;
    }
    else if (strcmp(symbol, "US30") == 0) {
        cfg.enabled = true;
        cfg.vwapProximityPct = 0.0014;
        cfg.maxHoldNs = 7'000'000'000ULL;
    }
    else if (strcmp(symbol, "XAUUSD") == 0) {
        cfg.enabled = true;
        cfg.vwapProximityPct = 0.0015;
        cfg.maxHoldNs = 8'000'000'000ULL;
    }
    else if (strcmp(symbol, "SPX500") == 0) {
        cfg.enabled = true;
        cfg.vwapProximityPct = 0.0012;
        cfg.maxHoldNs = 6'000'000'000ULL;
    }

    return cfg;
}

inline bool isVDSymbolEnabled(const char* symbol) {
    return getVDConfig(symbol).enabled;
}

// =============================================================================
// SESSION POLICY
// =============================================================================
struct VDSessionPolicy {
    bool enabled = false;
    double riskMultiplier = 1.0;
    int maxTradesPerSession = 15;

    bool isEnabled() const { return enabled; }
};

inline VDSessionPolicy getVDSessionPolicy(const std::string& session) {
    VDSessionPolicy policy;

    if (session == "NY" || session == "NEW_YORK") {
        policy.enabled = true;
        policy.riskMultiplier = 1.0;
        policy.maxTradesPerSession = 15;
    }
    else if (session == "LONDON" || session == "LDN") {
        policy.enabled = true;
        policy.riskMultiplier = 0.9;
        policy.maxTradesPerSession = 12;
    }
    else if (session == "ASIA") {
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
// MARKET SNAPSHOT (INPUT TO VWAP DEFENSE)
// =============================================================================
struct VDMarketSnapshot {
    // Identification
    const char* symbol = "";
    uint64_t now_ns = 0;

    // Price data
    double bid = 0.0;
    double ask = 0.0;
    double mid = 0.0;
    // v4.18.0: These are still in the snapshot for external callers,
    // but the profile uses its own VwapCalculator as authoritative source.
    double vwap = 0.0;
    double vwapSlope = 0.0;

    // Order book
    double imbalance = 0.0;

    // State flags
    bool latencyStable = false;
    bool shockActive = false;
    bool goNoGoIsGo = false;
    bool regimeToxic = false;
    bool structureResolving = false;
    const char* currentSession = "";

    // Computed helpers
    double spread() const { return ask - bid; }
    double vwapDistance() const { return std::abs(mid - vwap); }
    double vwapDistancePct() const { return mid > 0 ? vwapDistance() / mid : 0.0; }
    bool priceAboveVwap() const { return mid > vwap; }
    bool priceBelowVwap() const { return mid < vwap; }
};

// =============================================================================
// VWAP TEST STATE (per symbol)
// =============================================================================
struct VwapTestState {
    bool testing = false;
    bool wasAboveVwap = false;
    double testStartPrice = 0.0;
    uint64_t testStartNs = 0;
    uint64_t crossedVwapNs = 0;
    double crossedImbalance = 0.0;
    bool reclaimInProgress = false;
    bool failInProgress = false;

    void reset() {
        testing = false;
        wasAboveVwap = false;
        testStartPrice = 0.0;
        testStartNs = 0;
        crossedVwapNs = 0;
        crossedImbalance = 0.0;
        reclaimInProgress = false;
        failInProgress = false;
    }
};

// =============================================================================
// VWAP DEFENSE PROFILE
// =============================================================================
class VwapDefenseProfile {
public:
    VwapDefenseProfile();

    // =========================================================================
    // v4.18.0: TRADE DATA FEED — feeds VwapCalculator (authoritative VWAP)
    // Call this with every trade print for the symbol being tracked.
    // =========================================================================
    void onTrade(double price, double volume, uint64_t ts_ns);

    // =========================================================================
    // MAIN TICK HANDLER
    // =========================================================================
    void onTick(const VDMarketSnapshot& snap);

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
    // SESSION MANAGEMENT
    // =========================================================================
    void resetSession();
    void setSession(uint64_t startNs, uint64_t endNs);

    // =========================================================================
    // v4.18.0: SET TUNE (symbol-specific parameters)
    // =========================================================================
    void setTune(const VwapTune& t) { tune_ = t; tuneSet_ = true; }

    // =========================================================================
    // GETTERS
    // =========================================================================
    const char* name() const { return "VWAP_DEFENSE"; }
    VDState state() const { return state_; }
    VDIdleReason idleReason() const { return idleReason_; }
    bool hasPosition() const { return hasPosition_; }
    int tradesThisSession() const { return tradesThisSession_; }

    // v4.18.0: VWAP getters (from authoritative VwapCalculator)
    double vwap() const { return vwapCalc_.getVwap(); }
    double vwapSlope() const { return vwapCalc_.getSlope(); }
    bool vwapReady() const { return vwapCalc_.isWarmedUp(); }

    // =========================================================================
    // STATUS OUTPUT
    // =========================================================================
    void printStatus() const;
    void toJSON(char* buf, size_t buf_size) const;

    // =========================================================================
    // ENABLE/DISABLE
    // =========================================================================
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; state_ = VDState::IDLE; }
    bool isEnabled() const { return enabled_; }

private:
    // Core state
    VDState state_ = VDState::IDLE;
    VDIdleReason idleReason_ = VDIdleReason::NONE;
    uint64_t stateTs_ns_ = 0;

    // Per-symbol VWAP test tracking
    std::unordered_map<std::string, VwapTestState> tests_;

    // Position tracking
    bool hasPosition_ = false;
    VDSide positionSide_ = VDSide::NONE;
    VDEntryType entryType_ = VDEntryType::NONE;
    double entryPrice_ = 0.0;
    double entryVwap_ = 0.0;
    double entryEdge_ = 0.0;
    double posSize_ = 0.0;         // v4.18.0: tracked position size
    int pyramidAdds_ = 0;          // v4.18.0: pyramid add count
    uint64_t tradeStartNs_ = 0;
    std::string currentSymbol_;

    // Session tracking
    int tradesThisSession_ = 0;
    std::string currentSession_;
    VDSessionPolicy sessionPolicy_;

    // v4.18.0: VWAP — SEPARATE, AUTHORITATIVE
    VwapCalculator vwapCalc_;
    VwapState vwapState_;
    VwapTune tune_{};
    bool tuneSet_ = false;

    // v4.18.0: Session clock
    SessionClock sessionClock_;

    // Risk
    LossVelocity lossVelocity_;
    uint64_t lastTradeEndNs_ = 0;

    // Enable flag
    std::atomic<bool> enabled_{true};

    // =========================================================================
    // INTERNAL METHODS
    // =========================================================================
    bool detectVwapTest(const VDMarketSnapshot& snap);
    bool checkReclaim(const VDMarketSnapshot& snap);
    bool checkFailFade(const VDMarketSnapshot& snap);
    void evaluateEntry(const VDMarketSnapshot& snap);
    void evaluateExit(const VDMarketSnapshot& snap);

    // v4.18.0: pyramid add evaluation
    void tryPyramidAdd(const VDMarketSnapshot& snap);

    void enterTrade(VDSide side, VDEntryType type, const VDMarketSnapshot& snap);
    void exitTrade(const char* reason, const VDMarketSnapshot& snap);

    bool hardGatesPass(const VDMarketSnapshot& snap);

    // Output (virtual for override in actual implementation)
    virtual void submitOrder(VDSide side, double qty, const char* symbol) {
        printf("[VWAP_DEF] SUBMIT %s %.6f %s\n", vdSideToString(side), qty, symbol);
    }

    virtual void closePosition(const char* reason) {
        printf("[VWAP_DEF] CLOSE: %s\n", reason);
    }

    // Real time function
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // v4.18.0: Sizing via VwapPositionSizer when tune is set
    double calculateQty(double riskPct, const VDMarketSnapshot& snap) {
        if (tuneSet_) {
            double distPct = vwapCalc_.distancePct(snap.mid);
            return VwapPositionSizer::size(
                0.01,
                distPct,
                vwapState_.currentSlope,
                tune_.maxDistPct
            );
        }

        // Fallback: legacy sizing
        double spreadPts = snap.spread();
        if (spreadPts <= 0) spreadPts = 0.0001;

        double riskAmount = 10000.0 * riskPct;
        double stopDistPts = spreadPts * 2.5;
        double pointValue = 1.0;

        if (strstr(snap.symbol, "NAS") || strstr(snap.symbol, "SPX")) {
            pointValue = 1.0;
        } else if (strstr(snap.symbol, "XAU")) {
            pointValue = 0.1;
        }

        double qty = riskAmount / (stopDistPts * pointValue * 100.0);
        return std::clamp(qty, 0.01, 5.0);
    }
};

} // namespace Chimera
