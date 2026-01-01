// =============================================================================
// VwapDefenseProfile.cpp - v4.9.0 - VWAP DEFENSE IMPLEMENTATION
// =============================================================================
#include "profile/VwapDefenseProfile.hpp"

#include <cstring>
#include <cmath>

namespace Chimera {

VwapDefenseProfile::VwapDefenseProfile()
    : state_(VDState::IDLE),
      idleReason_(VDIdleReason::NONE),
      stateTs_ns_(0),
      hasPosition_(false),
      positionSide_(VDSide::NONE),
      entryType_(VDEntryType::NONE),
      entryPrice_(0.0),
      entryVwap_(0.0),
      entryEdge_(0.0),
      tradeStartNs_(0),
      tradesThisSession_(0),
      lastTradeEndNs_(0),
      enabled_(true)
{
}

void VwapDefenseProfile::resetSession() {
    tradesThisSession_ = 0;
    currentSession_.clear();
    sessionPolicy_ = VDSessionPolicy();
    lossVelocity_.reset();
    vwapState_ = VwapState();
    tests_.clear();
    
    state_ = VDState::IDLE;
    idleReason_ = VDIdleReason::NONE;
    
    printf("[VWAP_DEF] Session reset complete\n");
}

void VwapDefenseProfile::onTick(const VDMarketSnapshot& snap) {
    if (!enabled_) {
        idleReason_ = VDIdleReason::GO_NO_GO_BLOCK;
        if (state_ != VDState::IN_TRADE) {
            state_ = VDState::IDLE;
        }
        return;
    }
    
    // Update session policy if session changed
    if (currentSession_ != snap.currentSession) {
        currentSession_ = snap.currentSession;
        sessionPolicy_ = getVDSessionPolicy(currentSession_);
        tradesThisSession_ = 0;
    }
    
    // Update VWAP state
    vwapState_.update(snap.vwapSlope);
    
    // Check hard gates
    if (!hardGatesPass(snap)) {
        if (state_ != VDState::IN_TRADE) {
            state_ = VDState::IDLE;
        }
        return;
    }
    
    // State machine
    switch (state_) {
        case VDState::IDLE:
            if (detectVwapTest(snap)) {
                state_ = VDState::VWAP_TESTING;
                stateTs_ns_ = snap.now_ns;
                idleReason_ = VDIdleReason::NO_VWAP_TEST;
            } else {
                idleReason_ = VDIdleReason::NO_VWAP_TEST;
            }
            break;
            
        case VDState::VWAP_TESTING:
            evaluateEntry(snap);
            break;
            
        case VDState::RECLAIM_CONFIRMED:
            // Brief state - immediately enter
            {
                auto& test = tests_[snap.symbol];
                VDSide side;
                VDEntryType type;
                
                if (test.reclaimInProgress) {
                    // Reclaim: trade in direction of reclaim
                    side = test.wasAboveVwap ? VDSide::SELL : VDSide::BUY;
                    type = VDEntryType::RECLAIM;
                } else {
                    // Fail fade: trade against the failed break
                    side = test.wasAboveVwap ? VDSide::BUY : VDSide::SELL;
                    type = VDEntryType::FAIL_FADE;
                }
                
                enterTrade(side, type, snap);
            }
            break;
            
        case VDState::IN_TRADE:
            evaluateExit(snap);
            break;
            
        case VDState::COOLDOWN: {
            auto cfg = getVDConfig(snap.symbol);
            if (snap.now_ns - stateTs_ns_ > cfg.cooldownNs) {
                state_ = VDState::IDLE;
                idleReason_ = VDIdleReason::NONE;
                tests_[snap.symbol].reset();
            } else {
                idleReason_ = VDIdleReason::COOLDOWN_ACTIVE;
            }
            break;
        }
    }
}

bool VwapDefenseProfile::hardGatesPass(const VDMarketSnapshot& snap) {
    // 1. GoNoGo gate
    if (!snap.goNoGoIsGo) {
        idleReason_ = VDIdleReason::GO_NO_GO_BLOCK;
        return false;
    }
    
    // 2. Latency
    if (!snap.latencyStable) {
        idleReason_ = VDIdleReason::LATENCY_UNSTABLE;
        return false;
    }
    
    // 3. Session enabled
    if (!sessionPolicy_.isEnabled()) {
        idleReason_ = VDIdleReason::SESSION_DISABLED;
        return false;
    }
    
    // 4. Shock
    if (snap.shockActive) {
        idleReason_ = VDIdleReason::SHOCK_ACTIVE;
        return false;
    }
    
    // 5. Regime
    if (snap.regimeToxic) {
        idleReason_ = VDIdleReason::REGIME_TOXIC;
        return false;
    }
    
    // 6. Structure
    if (!snap.structureResolving) {
        idleReason_ = VDIdleReason::STRUCTURE_NOT_READY;
        return false;
    }
    
    // 7. Symbol enabled
    if (!isVDSymbolEnabled(snap.symbol)) {
        idleReason_ = VDIdleReason::SYMBOL_DISABLED;
        return false;
    }
    
    // 8. Trade limit
    if (tradesThisSession_ >= sessionPolicy_.maxTradesPerSession) {
        idleReason_ = VDIdleReason::TRADE_LIMIT_REACHED;
        return false;
    }
    
    // 9. Already have position
    if (hasPosition_) {
        idleReason_ = VDIdleReason::POSITION_OPEN;
        return true;  // Allow exit evaluation
    }
    
    return true;
}

bool VwapDefenseProfile::detectVwapTest(const VDMarketSnapshot& snap) {
    auto cfg = getVDConfig(snap.symbol);
    auto& test = tests_[snap.symbol];
    
    // Check if price is near VWAP
    double vwapDistPct = snap.vwapDistancePct();
    
    if (vwapDistPct > cfg.vwapProximityPct) {
        // Not near VWAP - reset test state
        if (test.testing) {
            test.reset();
        }
        return false;
    }
    
    // Price is near VWAP - start or continue test
    if (!test.testing) {
        test.testing = true;
        test.wasAboveVwap = snap.priceAboveVwap();
        test.testStartPrice = snap.mid;
        test.testStartNs = snap.now_ns;
        
        printf("[VWAP_DEF] VWAP test started %s price=%.5f vwap=%.5f (was %s)\n",
               snap.symbol, snap.mid, snap.vwap,
               test.wasAboveVwap ? "ABOVE" : "BELOW");
        return true;
    }
    
    return true;
}

void VwapDefenseProfile::evaluateEntry(const VDMarketSnapshot& snap) {
    auto& test = tests_[snap.symbol];
    auto cfg = getVDConfig(snap.symbol);
    
    if (!test.testing) {
        state_ = VDState::IDLE;
        idleReason_ = VDIdleReason::NO_VWAP_TEST;
        return;
    }
    
    // Track VWAP cross
    bool currentlyAbove = snap.priceAboveVwap();
    bool crossed = (currentlyAbove != test.wasAboveVwap);
    
    if (crossed && test.crossedVwapNs == 0) {
        test.crossedVwapNs = snap.now_ns;
        test.crossedImbalance = snap.imbalance;
        
        printf("[VWAP_DEF] VWAP crossed %s now=%s imb=%.2f\n",
               snap.symbol, currentlyAbove ? "ABOVE" : "BELOW", snap.imbalance);
    }
    
    // Check for reclaim (Variant A)
    if (checkReclaim(snap)) {
        test.reclaimInProgress = true;
        state_ = VDState::RECLAIM_CONFIRMED;
        stateTs_ns_ = snap.now_ns;
        
        printf("[VWAP_DEF] Reclaim confirmed %s\n", snap.symbol);
        return;
    }
    
    // Check for fail fade (Variant B)
    if (checkFailFade(snap)) {
        test.failInProgress = true;
        state_ = VDState::RECLAIM_CONFIRMED;  // Same next state
        stateTs_ns_ = snap.now_ns;
        
        printf("[VWAP_DEF] Fail fade confirmed %s\n", snap.symbol);
        return;
    }
    
    // Update idle reason based on what we're waiting for
    if (crossed) {
        if (currentlyAbove == test.wasAboveVwap) {
            idleReason_ = VDIdleReason::WAITING_RECLAIM;
        } else {
            idleReason_ = VDIdleReason::WAITING_FAIL;
        }
    }
    
    // Timeout - test expires
    uint64_t testDuration = snap.now_ns - test.testStartNs;
    if (testDuration > 2'000'000'000ULL) {  // 2 second timeout
        test.reset();
        state_ = VDState::IDLE;
        idleReason_ = VDIdleReason::NO_VWAP_TEST;
    }
}

bool VwapDefenseProfile::checkReclaim(const VDMarketSnapshot& snap) {
    auto& test = tests_[snap.symbol];
    auto cfg = getVDConfig(snap.symbol);
    
    if (test.crossedVwapNs == 0) return false;
    
    bool currentlyAbove = snap.priceAboveVwap();
    
    // Reclaim: price crossed VWAP and came back to original side
    bool reclaimed = (currentlyAbove == test.wasAboveVwap);
    if (!reclaimed) return false;
    
    // Must have crossed back - check timing
    uint64_t timeSinceCross = snap.now_ns - test.crossedVwapNs;
    
    // For reclaim, we need it to hold on the reclaimed side
    if (timeSinceCross < cfg.reclaimHoldNs) {
        return false;  // Not held long enough
    }
    
    // Check imbalance is supportive
    // For reclaim above VWAP, want positive imbalance
    // For reclaim below VWAP, want negative imbalance
    bool imbalanceSupportive = false;
    if (test.wasAboveVwap && snap.imbalance > cfg.supportiveImbalance) {
        imbalanceSupportive = true;
    }
    if (!test.wasAboveVwap && snap.imbalance < -cfg.supportiveImbalance) {
        imbalanceSupportive = true;
    }
    
    if (!imbalanceSupportive) return false;
    
    // Check VWAP slope supports (not against the reclaim)
    if (test.wasAboveVwap && vwapState_.direction == VwapDirection::DOWN) {
        return false;  // VWAP declining, don't buy reclaim
    }
    if (!test.wasAboveVwap && vwapState_.direction == VwapDirection::UP) {
        return false;  // VWAP rising, don't sell reclaim
    }
    
    return true;
}

bool VwapDefenseProfile::checkFailFade(const VDMarketSnapshot& snap) {
    auto& test = tests_[snap.symbol];
    auto cfg = getVDConfig(snap.symbol);
    
    if (test.crossedVwapNs == 0) return false;
    
    bool currentlyAbove = snap.priceAboveVwap();
    
    // Fail fade: price crossed VWAP and is still on opposite side but failing
    bool stillOpposite = (currentlyAbove != test.wasAboveVwap);
    if (!stillOpposite) return false;
    
    uint64_t timeSinceCross = snap.now_ns - test.crossedVwapNs;
    
    // Fail must happen within the window
    if (timeSinceCross > cfg.failWindowNs) {
        return false;  // Too late for fail fade
    }
    
    // Check for failure signs:
    // 1. Imbalance collapsed
    bool imbalanceCollapsed = std::abs(snap.imbalance) < cfg.collapseImbalance;
    
    // 2. Price returning toward VWAP
    bool returningToVwap = snap.vwapDistancePct() < test.crossedImbalance * 0.5;
    
    // 3. Imbalance flipped
    bool imbalanceFlipped = false;
    if (test.wasAboveVwap) {
        // Was above, crossed below, imbalance should flip positive (buy pressure to push back)
        imbalanceFlipped = (test.crossedImbalance < -0.3 && snap.imbalance > 0.2);
    } else {
        // Was below, crossed above, imbalance should flip negative
        imbalanceFlipped = (test.crossedImbalance > 0.3 && snap.imbalance < -0.2);
    }
    
    // Need at least 2 of 3 failure signs
    int failureScore = (imbalanceCollapsed ? 1 : 0) + 
                       (returningToVwap ? 1 : 0) + 
                       (imbalanceFlipped ? 1 : 0);
    
    return failureScore >= 2;
}

void VwapDefenseProfile::enterTrade(VDSide side, VDEntryType type, const VDMarketSnapshot& snap) {
    // Calculate position size (0.05% - 0.10% risk)
    double baseRisk = 0.0007;  // 0.07%
    double adjustedRisk = baseRisk * sessionPolicy_.riskMultiplier;
    double qty = calculateQty(adjustedRisk, snap);
    
    // Submit order
    submitOrder(side, qty, snap.symbol);
    
    // Update state
    hasPosition_ = true;
    positionSide_ = side;
    entryType_ = type;
    entryPrice_ = (side == VDSide::BUY) ? snap.ask : snap.bid;
    entryVwap_ = snap.vwap;
    entryEdge_ = snap.vwapDistancePct();
    tradeStartNs_ = snap.now_ns;
    currentSymbol_ = snap.symbol;
    tradesThisSession_++;
    
    state_ = VDState::IN_TRADE;
    idleReason_ = VDIdleReason::NONE;
    
    printf("[VWAP_DEF] ENTRY %s %s @ %.5f | Type: %s | VWAP: %.5f | Trade #%d\n",
           vdSideToString(side), snap.symbol, entryPrice_,
           vdEntryTypeToString(type), snap.vwap, tradesThisSession_);
}

void VwapDefenseProfile::evaluateExit(const VDMarketSnapshot& snap) {
    if (!hasPosition_) return;
    if (currentSymbol_ != snap.symbol) return;
    
    auto cfg = getVDConfig(currentSymbol_);
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    // Exit triggers (FIRST HIT WINS)
    
    // 1. Time cap (5-8s)
    if (heldNs > cfg.maxHoldNs) {
        exitTrade("TIME_CAP", snap);
        return;
    }
    
    // 2. VWAP reclaimed against position
    if (positionSide_ == VDSide::BUY && snap.priceBelowVwap()) {
        // Long but price fell below VWAP
        if (snap.vwapDistancePct() > cfg.vwapProximityPct * 2) {
            exitTrade("VWAP_LOST", snap);
            return;
        }
    }
    if (positionSide_ == VDSide::SELL && snap.priceAboveVwap()) {
        // Short but price rose above VWAP
        if (snap.vwapDistancePct() > cfg.vwapProximityPct * 2) {
            exitTrade("VWAP_LOST", snap);
            return;
        }
    }
    
    // 3. Edge decay > 45%
    double currentEdge = snap.vwapDistancePct();
    if (entryEdge_ > 0) {
        double edgeDecay = 1.0 - (currentEdge / entryEdge_);
        if (edgeDecay > cfg.edgeDecayExit) {
            exitTrade("EDGE_DECAY", snap);
            return;
        }
    }
    
    // 4. Latency degradation
    if (!snap.latencyStable) {
        exitTrade("LATENCY_DEGRADED", snap);
        return;
    }
}

void VwapDefenseProfile::exitTrade(const char* reason, const VDMarketSnapshot& snap) {
    closePosition(reason);
    
    double exitPrice = (positionSide_ == VDSide::BUY) ? snap.bid : snap.ask;
    double pnlBps = 0.0;
    if (entryPrice_ > 0) {
        pnlBps = (exitPrice - entryPrice_) / entryPrice_ * 10000.0;
        if (positionSide_ == VDSide::SELL) pnlBps = -pnlBps;
    }
    
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    double heldMs = static_cast<double>(heldNs) / 1e6;
    
    printf("[VWAP_DEF] EXIT %s: %s @ %.5f | Type: %s | PnL: %.2f bps | Held: %.0f ms\n",
           currentSymbol_.c_str(), reason, exitPrice,
           vdEntryTypeToString(entryType_), pnlBps, heldMs);
    
    // Update loss tracking
    if (pnlBps < -0.05) {
        lossVelocity_.recordLoss(snap.now_ns);
    }
    
    // Reset position state
    hasPosition_ = false;
    positionSide_ = VDSide::NONE;
    entryType_ = VDEntryType::NONE;
    entryPrice_ = 0.0;
    entryVwap_ = 0.0;
    entryEdge_ = 0.0;
    lastTradeEndNs_ = snap.now_ns;
    
    // Enter cooldown
    state_ = VDState::COOLDOWN;
    stateTs_ns_ = snap.now_ns;
    idleReason_ = VDIdleReason::COOLDOWN_ACTIVE;
}

void VwapDefenseProfile::onFill(double fillPrice, double qty, bool isBuy) {
    printf("[VWAP_DEF] FILL: %s %.6f @ %.5f\n",
           isBuy ? "BUY" : "SELL", qty, fillPrice);
}

void VwapDefenseProfile::onPositionClosed(double pnl_bps, uint64_t heldNs) {
    (void)pnl_bps;
    (void)heldNs;
}

void VwapDefenseProfile::onTimer(uint64_t now_ns) {
    (void)now_ns;
}

void VwapDefenseProfile::printStatus() const {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  VWAP DEFENSE STATUS                                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  State:     %-15s                                     ║\n", vdStateToString(state_));
    printf("║  Idle:      %s %-20s                          ║\n",
           vdIdleReasonIcon(idleReason_), vdIdleReasonToString(idleReason_));
    printf("║  Session:   %-10s (%.1fx risk, %d/%d trades)           ║\n",
           currentSession_.c_str(), sessionPolicy_.riskMultiplier,
           tradesThisSession_, sessionPolicy_.maxTradesPerSession);
    printf("║  Position:  %s                                              ║\n",
           hasPosition_ ? "YES" : "NO");
    printf("║  Entry:     %s                                           ║\n",
           vdEntryTypeToString(entryType_));
    printf("║  Enabled:   %s                                              ║\n",
           enabled_ ? "YES" : "NO");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void VwapDefenseProfile::toJSON(char* buf, size_t buf_size) const {
    snprintf(buf, buf_size,
        "{"
        "\"profile\":\"VWAP_DEFENSE\","
        "\"state\":\"%s\","
        "\"idle_reason\":\"%s\","
        "\"session\":\"%s\","
        "\"risk_mult\":%.2f,"
        "\"trades_session\":%d,"
        "\"max_trades\":%d,"
        "\"has_position\":%s,"
        "\"entry_type\":\"%s\","
        "\"enabled\":%s"
        "}",
        vdStateToString(state_),
        vdIdleReasonToString(idleReason_),
        currentSession_.c_str(),
        sessionPolicy_.riskMultiplier,
        tradesThisSession_,
        sessionPolicy_.maxTradesPerSession,
        hasPosition_ ? "true" : "false",
        vdEntryTypeToString(entryType_),
        enabled_.load() ? "true" : "false"
    );
}

} // namespace Chimera
