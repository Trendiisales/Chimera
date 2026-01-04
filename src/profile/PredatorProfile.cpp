// =============================================================================
// PredatorProfile.cpp - v4.8.0 - PREDATOR PROFILE IMPLEMENTATION
// =============================================================================
#include "profile/PredatorProfile.hpp"

#include <cstring>
#include <cmath>

namespace Chimera {

PredatorProfile::PredatorProfile()
    : state_(PredatorState::IDLE),
      idleReason_(PredatorIdleReason::NONE),
      stateTs_ns_(0),
      hasPosition_(false),
      positionSide_(Side::NONE),
      entryPrice_(0.0),
      entryEdge_(0.0),
      tradeStartNs_(0),
      tradesThisSession_(0),
      lastTradeEndNs_(0),
      enabled_(true)
{
}

void PredatorProfile::resetSession() {
    tradesThisSession_ = 0;
    currentSession_.clear();
    sessionPolicy_ = {PredatorAggression::OFF, 0.0, 0};
    lossVelocity_.reset();
    consecutiveLosses_.reset();
    vwapAccelState_.reset();
    state_ = PredatorState::IDLE;
    idleReason_ = PredatorIdleReason::NONE;
}

void PredatorProfile::onTick(const PredatorMarketSnapshot& snap) {
    // Check if enabled
    if (!enabled_) {
        idleReason_ = PredatorIdleReason::GO_NO_GO_BLOCK;
        state_ = PredatorState::IDLE;
        return;
    }
    
    // Update session policy if session changed
    if (currentSession_ != snap.currentSession) {
        currentSession_ = snap.currentSession;
        sessionPolicy_ = getPredatorSessionPolicy(currentSession_);
        tradesThisSession_ = 0;
    }
    
    // Check hard gates
    if (!hardGatesPass(snap)) {
        if (state_ != PredatorState::IN_TRADE) {
            state_ = PredatorState::IDLE;
        }
        return;
    }
    
    // State machine
    switch (state_) {
        case PredatorState::IDLE:
            // Transition to ARMED if all gates pass
            state_ = PredatorState::ARMED;
            stateTs_ns_ = snap.now_ns;
            idleReason_ = PredatorIdleReason::NONE;
            break;

        case PredatorState::ARMED:
            evaluateEntry(snap);
            break;

        case PredatorState::IN_TRADE:
            evaluateExit(snap);
            break;

        case PredatorState::COOLDOWN: {
            // Adaptive cooldown based on loss velocity
            uint64_t cooldownDuration = lossVelocity_.getAdaptiveCooldown(snap.now_ns);
            if (snap.now_ns - stateTs_ns_ > cooldownDuration) {
                state_ = PredatorState::IDLE;
                idleReason_ = PredatorIdleReason::NONE;
            } else {
                idleReason_ = PredatorIdleReason::COOLDOWN_ACTIVE;
            }
            break;
        }
    }
}

bool PredatorProfile::hardGatesPass(const PredatorMarketSnapshot& snap) {
    // Priority order (highest first)
    
    // 1. GoNoGo gate
    if (!snap.goNoGoIsGo) {
        idleReason_ = PredatorIdleReason::GO_NO_GO_BLOCK;
        return false;
    }
    
    // 2. Latency
    if (!snap.latencyStable) {
        idleReason_ = PredatorIdleReason::LATENCY_UNSTABLE;
        return false;
    }
    
    // 3. Session enabled
    if (!sessionPolicy_.isEnabled()) {
        idleReason_ = PredatorIdleReason::SESSION_DISABLED;
        return false;
    }
    
    // 4. Regime
    if (snap.regimeToxic) {
        idleReason_ = PredatorIdleReason::REGIME_TOXIC;
        return false;
    }
    
    // 5. Shock
    if (snap.shockActive) {
        idleReason_ = PredatorIdleReason::REGIME_TOXIC;
        return false;
    }
    
    // 6. Structure
    if (!snap.structureResolving) {
        idleReason_ = PredatorIdleReason::STRUCTURE_NOT_READY;
        return false;
    }
    
    // 7. Symbol enabled
    if (!isPredatorSymbolEnabled(snap.symbol)) {
        idleReason_ = PredatorIdleReason::SYMBOL_DISABLED;
        return false;
    }
    
    // 8. Trade limit
    if (tradesThisSession_ >= sessionPolicy_.maxTrades) {
        idleReason_ = PredatorIdleReason::TRADE_LIMIT_REACHED;
        return false;
    }
    
    // 9. Consecutive losses
    if (consecutiveLosses_.shouldDisable()) {
        idleReason_ = PredatorIdleReason::CONSECUTIVE_LOSSES;
        disable();
        return false;
    }
    
    // 10. Already have position
    if (hasPosition_) {
        idleReason_ = PredatorIdleReason::POSITION_OPEN;
        return true;  // Allow exit evaluation
    }
    
    return true;
}

void PredatorProfile::evaluateEntry(const PredatorMarketSnapshot& snap) {
    // Get symbol config
    auto cfg = getPredatorConfig(snap.symbol);
    if (!cfg.enabled) {
        idleReason_ = PredatorIdleReason::SYMBOL_DISABLED;
        return;
    }
    
    // Check VWAP acceleration filter
    if (!vwapAccelerating(snap.vwapSlope, vwapAccelState_, 0.00015)) {
        idleReason_ = PredatorIdleReason::EDGE_NOT_PRESENT;
        return;
    }
    
    // Check cooldown (adaptive based on loss velocity)
    if (lossVelocity_.inCooldown(snap.now_ns, lastTradeEndNs_)) {
        idleReason_ = PredatorIdleReason::COOLDOWN_ACTIVE;
        return;
    }
    
    // Entry Type A: Imbalance Snapback
    if (checkImbalanceSnapback(snap)) {
        // Fade the failed imbalance
        Side side = (snap.imbalance > 0) ? Side::SELL : Side::BUY;
        enterTrade(side, snap);
        return;
    }
    
    // Entry Type B: Micro Break + Immediate Acceptance
    if (checkMicroBreakAcceptance(snap)) {
        Side side = (snap.breakDirection > 0) ? Side::BUY : Side::SELL;
        enterTrade(side, snap);
        return;
    }
    
    // No valid entry
    idleReason_ = PredatorIdleReason::EDGE_NOT_PRESENT;
}

bool PredatorProfile::checkImbalanceSnapback(const PredatorMarketSnapshot& snap) const {
    auto cfg = getPredatorConfig(snap.symbol);
    
    // Conditions:
    // 1. OrderBookImbalance ≥ minImbalance
    // 2. Price fails to continue within maxAcceptMs
    // 3. Book refills ≥ 65% inside 200ms
    // 4. VWAP slope flattens or reverses
    
    // Check imbalance threshold
    if (std::abs(snap.imbalance) < cfg.minImbalance) {
        return false;
    }
    
    // Check book refill (indicating failed continuation)
    if (snap.bookRefillRatio < 0.65) {
        return false;
    }
    
    // Check VWAP flattening
    // Imbalance > 0 means buy pressure, so VWAP should be flattening (not accelerating up)
    // Imbalance < 0 means sell pressure, so VWAP should be flattening (not accelerating down)
    VwapDirection dir = getVwapDirection(snap.vwapSlope);
    if (snap.imbalance > 0 && dir == VwapDirection::UP) {
        return false;  // Buy pressure but VWAP still accelerating up - not a snapback
    }
    if (snap.imbalance < 0 && dir == VwapDirection::DOWN) {
        return false;  // Sell pressure but VWAP still accelerating down - not a snapback
    }
    
    return true;
}

bool PredatorProfile::checkMicroBreakAcceptance(const PredatorMarketSnapshot& snap) const {
    auto cfg = getPredatorConfig(snap.symbol);
    
    // Conditions:
    // 1. Micro range break (last 500–800ms)
    // 2. Follow-through ≥ 2 ticks within 150ms
    // 3. No VWAP rejection
    
    // Check for micro break
    if (!snap.microRangeBreak) {
        return false;
    }
    
    // Check break age (500-800ms)
    uint64_t breakAgeMs = snap.breakAge_ns / 1'000'000ULL;
    if (breakAgeMs < 50 || breakAgeMs > 800) {  // Too fresh or too old
        return false;
    }
    
    // Check follow-through (at least 2 ticks)
    if (snap.followThroughTicks < 2) {
        return false;
    }
    
    // Check acceptance time
    if (breakAgeMs > cfg.maxAcceptMs) {
        return false;  // Took too long to accept
    }
    
    // Check VWAP not rejecting
    if (snap.vwapReclaimDetected) {
        return false;  // VWAP rejection
    }
    
    return true;
}

void PredatorProfile::enterTrade(Side side, const PredatorMarketSnapshot& snap) {
    // Calculate position size with session risk multiplier
    double baseRisk = 0.001;  // 0.10%
    double adjustedRisk = baseRisk * sessionPolicy_.riskMultiplier;
    double qty = calculateQty(adjustedRisk, snap);
    
    // Submit order
    submitOrder(side, qty, snap.symbol);
    
    // Update state
    hasPosition_ = true;
    positionSide_ = side;
    entryPrice_ = (side == Side::BUY) ? snap.ask : snap.bid;
    entryEdge_ = snap.currentEdge;
    tradeStartNs_ = snap.now_ns;
    currentSymbol_ = snap.symbol;
    tradesThisSession_++;
    
    state_ = PredatorState::IN_TRADE;
    idleReason_ = PredatorIdleReason::NONE;
    
    printf("[PREDATOR] ENTRY %s %s @ %.5f | Edge: %.4f | Trade #%d/%d\n",
           sideToString(side), snap.symbol, entryPrice_, entryEdge_,
           tradesThisSession_, sessionPolicy_.maxTrades);
}

void PredatorProfile::evaluateExit(const PredatorMarketSnapshot& snap) {
    if (!hasPosition_) return;
    
    auto cfg = getPredatorConfig(currentSymbol_);
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    // Exit triggers (FIRST HIT WINS)
    
    // 1. Time cap (symbol specific)
    if (heldNs > cfg.maxHoldNs) {
        exitTrade("TIME_CAP", snap);
        return;
    }
    
    // 2. Edge decay (symbol specific)
    if (entryEdge_ > 0 && snap.currentEdge < entryEdge_ * (1.0 - cfg.edgeDecayExit)) {
        exitTrade("EDGE_DECAY", snap);
        return;
    }
    
    // 3. Imbalance flip against position
    if (snap.imbalanceFlipped) {
        bool flippedAgainst = (positionSide_ == Side::BUY && snap.imbalance < -0.3) ||
                              (positionSide_ == Side::SELL && snap.imbalance > 0.3);
        if (flippedAgainst) {
            exitTrade("IMBALANCE_FLIP", snap);
            return;
        }
    }
    
    // 4. VWAP micro reclaim
    if (snap.vwapReclaimDetected) {
        exitTrade("VWAP_RECLAIM", snap);
        return;
    }
    
    // 5. Latency degradation
    if (snap.latencyDegraded()) {
        exitTrade("LATENCY_DEGRADED", snap);
        return;
    }
}

void PredatorProfile::exitTrade(const char* reason, const PredatorMarketSnapshot& snap) {
    // Close the position
    closePosition(reason);
    
    // Calculate PnL
    double exitPrice = (positionSide_ == Side::BUY) ? snap.bid : snap.ask;
    double pnlBps = 0.0;
    if (entryPrice_ > 0) {
        pnlBps = (exitPrice - entryPrice_) / entryPrice_ * 10000.0;
        if (positionSide_ == Side::SELL) pnlBps = -pnlBps;
    }
    
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    printf("[PREDATOR] EXIT %s: %s @ %.5f | PnL: %.2f bps | Held: %.0f ms\n",
           currentSymbol_.c_str(), reason, exitPrice, pnlBps,
           static_cast<double>(heldNs) / 1e6);
    
    // Update expectancy tracking
    getPredatorExpectancy().recordTrade(currentSymbol_, pnlBps, heldNs);
    
    // Update loss tracking
    if (pnlBps < -0.05) {
        lossVelocity_.recordLoss(snap.now_ns);
        consecutiveLosses_.recordLoss();
    } else if (pnlBps > 0.05) {
        consecutiveLosses_.recordWin();
    }
    
    // Reset position state
    hasPosition_ = false;
    positionSide_ = Side::NONE;
    entryPrice_ = 0.0;
    entryEdge_ = 0.0;
    lastTradeEndNs_ = snap.now_ns;
    
    // Enter cooldown
    state_ = PredatorState::COOLDOWN;
    stateTs_ns_ = snap.now_ns;
    idleReason_ = PredatorIdleReason::COOLDOWN_ACTIVE;
}

void PredatorProfile::onFill(double fillPrice, double qty, bool isBuy) {
    printf("[PREDATOR] FILL: %s %.6f @ %.5f\n",
           isBuy ? "BUY" : "SELL", qty, fillPrice);
}

void PredatorProfile::onPositionClosed(double pnl_bps, uint64_t heldNs) {
    // External notification of position close
    getPredatorExpectancy().recordTrade(currentSymbol_, pnl_bps, heldNs);
    
    if (pnl_bps < -0.05) {
        consecutiveLosses_.recordLoss();
    } else if (pnl_bps > 0.05) {
        consecutiveLosses_.recordWin();
    }
}

void PredatorProfile::onTimer(uint64_t now_ns) {
    // Periodic maintenance (e.g., every second)
    (void)now_ns;
}

void PredatorProfile::printStatus() const {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  PREDATOR STATUS                                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  State:    %-12s                                        ║\n", predatorStateToString(state_));
    printf("║  Idle:     %s %-20s                         ║\n",
           predatorIdleReasonIcon(idleReason_), predatorIdleReasonToString(idleReason_));
    printf("║  Session:  %-10s (%.1fx risk, %d/%d trades)              ║\n",
           currentSession_.c_str(), sessionPolicy_.riskMultiplier,
           tradesThisSession_, sessionPolicy_.maxTrades);
    printf("║  Position: %s                                               ║\n",
           hasPosition_ ? "YES" : "NO");
    printf("║  Enabled:  %s                                               ║\n",
           enabled_ ? "YES" : "NO");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    consecutiveLosses_.print();
}

void PredatorProfile::toJSON(char* buf, size_t buf_size) const {
    snprintf(buf, buf_size,
        "{"
        "\"profile\":\"PREDATOR\","
        "\"state\":\"%s\","
        "\"idle_reason\":\"%s\","
        "\"session\":\"%s\","
        "\"risk_mult\":%.2f,"
        "\"trades_session\":%d,"
        "\"max_trades\":%d,"
        "\"has_position\":%s,"
        "\"enabled\":%s"
        "}",
        predatorStateToString(state_),
        predatorIdleReasonToString(idleReason_),
        currentSession_.c_str(),
        sessionPolicy_.riskMultiplier,
        tradesThisSession_,
        sessionPolicy_.maxTrades,
        hasPosition_ ? "true" : "false",
        enabled_.load() ? "true" : "false"
    );
}

} // namespace Chimera
