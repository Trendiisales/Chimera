// =============================================================================
// StopRunFadeProfile.cpp - v4.9.0 - STOP RUN FADE IMPLEMENTATION
// =============================================================================
#include "profile/StopRunFadeProfile.hpp"

#include <cstring>
#include <cmath>

namespace Chimera {

StopRunFadeProfile::StopRunFadeProfile()
    : state_(SRState::IDLE),
      idleReason_(SRIdleReason::NONE),
      stateTs_ns_(0),
      hasPosition_(false),
      positionSide_(SRSide::NONE),
      entryPrice_(0.0),
      entryVwap_(0.0),
      tradeStartNs_(0),
      tradesThisSession_(0),
      lastTradeEndNs_(0),
      enabled_(true)
{
}

void StopRunFadeProfile::resetSession() {
    tradesThisSession_ = 0;
    currentSession_.clear();
    sessionPolicy_ = SRSessionPolicy();
    lossVelocity_.reset();
    vwapState_ = VwapState();
    
    // Clear per-symbol tracking
    runs_.clear();
    velocityTrackers_.clear();
    // Don't clear baselineTrackers - they need continuity
    
    state_ = SRState::IDLE;
    idleReason_ = SRIdleReason::NONE;
    
    printf("[STOP_RUN] Session reset complete\n");
}

void StopRunFadeProfile::onTick(const SRMarketSnapshot& snap) {
    // Check if enabled
    if (!enabled_) {
        idleReason_ = SRIdleReason::GO_NO_GO_BLOCK;
        if (state_ != SRState::IN_TRADE) {
            state_ = SRState::IDLE;
        }
        return;
    }
    
    // Update session policy if session changed
    if (currentSession_ != snap.currentSession) {
        currentSession_ = snap.currentSession;
        sessionPolicy_ = getSRSessionPolicy(currentSession_);
        tradesThisSession_ = 0;
    }
    
    // Update VWAP state
    vwapState_.update(snap.vwapSlope);
    
    // Update velocity tracker
    velocityTrackers_[snap.symbol].addTick(snap.mid, snap.now_ns);
    
    // Update baseline range tracker
    double currentRange = velocityTrackers_[snap.symbol].getRange();
    baselineTrackers_[snap.symbol].update(currentRange);
    
    // Check hard gates
    if (!hardGatesPass(snap)) {
        if (state_ != SRState::IN_TRADE) {
            state_ = SRState::IDLE;
        }
        return;
    }
    
    // State machine
    switch (state_) {
        case SRState::IDLE:
            if (detectStopRun(snap)) {
                state_ = SRState::RUN_DETECTED;
                stateTs_ns_ = snap.now_ns;
                idleReason_ = SRIdleReason::WAITING_FAILURE;
                
                printf("[STOP_RUN] Run detected %s dir=%d price=%.2f imb=%.2f\n",
                       snap.symbol, runs_[snap.symbol].direction,
                       snap.mid, snap.imbalance);
            } else {
                idleReason_ = SRIdleReason::NO_RUN_DETECTED;
            }
            break;
            
        case SRState::RUN_DETECTED:
            evaluateEntry(snap);
            break;
            
        case SRState::CONFIRM_FAIL:
            // This state is brief - immediately enter
            {
                auto& run = runs_[snap.symbol];
                SRSide side = (run.direction > 0) ? SRSide::SELL : SRSide::BUY;
                enterTrade(side, snap);
            }
            break;
            
        case SRState::IN_TRADE:
            evaluateExit(snap);
            break;
            
        case SRState::COOLDOWN: {
            auto cfg = getSRConfig(snap.symbol);
            if (snap.now_ns - stateTs_ns_ > cfg.cooldownNs) {
                state_ = SRState::IDLE;
                idleReason_ = SRIdleReason::NONE;
                runs_[snap.symbol].reset();
            } else {
                idleReason_ = SRIdleReason::COOLDOWN_ACTIVE;
            }
            break;
        }
    }
}

bool StopRunFadeProfile::hardGatesPass(const SRMarketSnapshot& snap) {
    // 1. GoNoGo gate
    if (!snap.goNoGoIsGo) {
        idleReason_ = SRIdleReason::GO_NO_GO_BLOCK;
        return false;
    }
    
    // 2. Latency
    if (!snap.latencyStable) {
        idleReason_ = SRIdleReason::LATENCY_UNSTABLE;
        return false;
    }
    
    // 3. Session enabled
    if (!sessionPolicy_.isEnabled()) {
        idleReason_ = SRIdleReason::SESSION_DISABLED;
        return false;
    }
    
    // 4. Shock
    if (snap.shockActive) {
        idleReason_ = SRIdleReason::SHOCK_ACTIVE;
        return false;
    }
    
    // 5. Symbol enabled
    if (!isSRSymbolEnabled(snap.symbol)) {
        idleReason_ = SRIdleReason::SYMBOL_DISABLED;
        return false;
    }
    
    // 6. Trade limit
    if (tradesThisSession_ >= sessionPolicy_.maxTradesPerSession) {
        idleReason_ = SRIdleReason::DAILY_LIMIT_REACHED;
        return false;
    }
    
    // 7. Already have position
    if (hasPosition_) {
        idleReason_ = SRIdleReason::POSITION_OPEN;
        return true;  // Allow exit evaluation
    }
    
    return true;
}

bool StopRunFadeProfile::detectStopRun(const SRMarketSnapshot& snap) {
    auto cfg = getSRConfig(snap.symbol);
    auto& run = runs_[snap.symbol];
    auto& velTracker = velocityTrackers_[snap.symbol];
    auto& baselineTracker = baselineTrackers_[snap.symbol];
    
    // Calculate current velocity
    double velocity = velTracker.getVelocity();
    double currentRange = velTracker.getRange();
    double baselineRange = baselineTracker.get();
    
    // Condition 1: Velocity spike
    if (velocity < cfg.velocityThreshold) {
        return false;
    }
    
    // Condition 2: Range expansion
    if (baselineRange <= 0.0) return false;
    double rangeExpansion = currentRange / baselineRange;
    if (rangeExpansion < cfg.rangeExpansionMult) {
        return false;
    }
    
    // Condition 3: Extreme imbalance
    if (std::abs(snap.imbalance) < cfg.extremeImbalance) {
        return false;
    }
    
    // All conditions met - stop run detected!
    run.detected = true;
    run.direction = (snap.imbalance > 0) ? +1 : -1;  // Positive imbalance = run up
    run.runStartPrice = snap.mid;
    run.runPeakPrice = snap.mid;
    run.runStartNs = snap.now_ns;
    run.peakImbalance = snap.imbalance;
    run.baselineRange = baselineRange;
    
    return true;
}

void StopRunFadeProfile::evaluateEntry(const SRMarketSnapshot& snap) {
    auto& run = runs_[snap.symbol];
    auto cfg = getSRConfig(snap.symbol);
    
    if (!run.detected) {
        state_ = SRState::IDLE;
        idleReason_ = SRIdleReason::NO_RUN_DETECTED;
        return;
    }
    
    uint64_t timeSinceRun = snap.now_ns - run.runStartNs;
    
    // Update peak price
    if (run.direction > 0) {
        run.runPeakPrice = std::max(run.runPeakPrice, snap.mid);
    } else {
        run.runPeakPrice = std::min(run.runPeakPrice, snap.mid);
    }
    
    // Check for failure confirmation
    if (confirmFailure(snap)) {
        state_ = SRState::CONFIRM_FAIL;
        stateTs_ns_ = snap.now_ns;
        
        printf("[STOP_RUN] Failure confirmed %s - imbalance collapsed %.2f → %.2f\n",
               snap.symbol, run.peakImbalance, snap.imbalance);
        return;
    }
    
    // Check if run continued (invalidates the setup)
    // If price continues beyond peak AND time > failure window, the run is valid
    if (timeSinceRun > cfg.failureWindowNs) {
        bool runContinued = false;
        
        if (run.direction > 0 && snap.mid > run.runPeakPrice) {
            runContinued = true;
        } else if (run.direction < 0 && snap.mid < run.runPeakPrice) {
            runContinued = true;
        }
        
        // Also check if imbalance is still extreme (run continuing)
        if (std::abs(snap.imbalance) >= cfg.extremeImbalance * 0.9) {
            runContinued = true;
        }
        
        if (runContinued) {
            // Run continued - no fade opportunity
            run.reset();
            state_ = SRState::IDLE;
            idleReason_ = SRIdleReason::RUN_CONTINUED;
            
            printf("[STOP_RUN] Run continued for %s - no fade\n", snap.symbol);
            return;
        }
    }
    
    // Timeout - run detection expires
    if (timeSinceRun > 500'000'000ULL) {  // 500ms timeout
        run.reset();
        state_ = SRState::IDLE;
        idleReason_ = SRIdleReason::NO_RUN_DETECTED;
    }
}

bool StopRunFadeProfile::confirmFailure(const SRMarketSnapshot& snap) {
    auto& run = runs_[snap.symbol];
    auto cfg = getSRConfig(snap.symbol);
    
    if (!run.detected) return false;
    
    uint64_t timeSinceRun = snap.now_ns - run.runStartNs;
    
    // Must be within failure window
    if (timeSinceRun > cfg.failureWindowNs) {
        return false;
    }
    
    // Condition 1: Imbalance collapsed
    if (std::abs(snap.imbalance) > cfg.imbalanceCollapse) {
        return false;  // Imbalance still extreme
    }
    
    // Condition 2: Price stopped advancing (no continuation)
    bool priceStalled = false;
    if (run.direction > 0) {
        // Run was up, price should have stopped or reversed
        priceStalled = (snap.mid <= run.runPeakPrice);
    } else {
        // Run was down, price should have stopped or reversed
        priceStalled = (snap.mid >= run.runPeakPrice);
    }
    
    if (!priceStalled) {
        return false;
    }
    
    // Condition 3: VWAP rejects (optional but improves quality)
    // For run up, VWAP should not be accelerating up
    // For run down, VWAP should not be accelerating down
    bool vwapRejects = false;
    if (run.direction > 0 && vwapState_.direction != VwapDirection::UP) {
        vwapRejects = true;
    } else if (run.direction < 0 && vwapState_.direction != VwapDirection::DOWN) {
        vwapRejects = true;
    }
    
    // All conditions met
    return vwapRejects;
}

void StopRunFadeProfile::enterTrade(SRSide side, const SRMarketSnapshot& snap) {
    // Calculate position size (0.05% - 0.10% risk)
    double baseRisk = 0.0005;  // 0.05%
    double adjustedRisk = baseRisk * sessionPolicy_.riskMultiplier;
    double qty = calculateQty(adjustedRisk, snap);
    
    // Submit order
    submitOrder(side, qty, snap.symbol);
    
    // Update state
    hasPosition_ = true;
    positionSide_ = side;
    entryPrice_ = (side == SRSide::BUY) ? snap.ask : snap.bid;
    entryVwap_ = snap.vwap;
    tradeStartNs_ = snap.now_ns;
    currentSymbol_ = snap.symbol;
    tradesThisSession_++;
    
    state_ = SRState::IN_TRADE;
    idleReason_ = SRIdleReason::NONE;
    
    auto& run = runs_[snap.symbol];
    printf("[STOP_RUN] ENTRY %s %s @ %.5f | Fade run %s | Imb: %.2f→%.2f | Trade #%d\n",
           srSideToString(side), snap.symbol, entryPrice_,
           (run.direction > 0) ? "UP" : "DOWN",
           run.peakImbalance, snap.imbalance,
           tradesThisSession_);
}

void StopRunFadeProfile::evaluateExit(const SRMarketSnapshot& snap) {
    if (!hasPosition_) return;
    if (currentSymbol_ != snap.symbol) return;
    
    auto cfg = getSRConfig(currentSymbol_);
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    // Exit triggers (FIRST HIT WINS)
    
    // 1. Time cap (3s default)
    if (heldNs > cfg.maxHoldNs) {
        exitTrade("TIME_CAP", snap);
        return;
    }
    
    // 2. VWAP touch (profit target)
    if (positionSide_ == SRSide::BUY && snap.mid >= entryVwap_) {
        exitTrade("VWAP_TOUCH", snap);
        return;
    }
    if (positionSide_ == SRSide::SELL && snap.mid <= entryVwap_) {
        exitTrade("VWAP_TOUCH", snap);
        return;
    }
    
    // 3. Imbalance flip against position
    bool imbalanceAgainst = false;
    if (positionSide_ == SRSide::BUY && snap.imbalance < -0.5) {
        imbalanceAgainst = true;
    }
    if (positionSide_ == SRSide::SELL && snap.imbalance > 0.5) {
        imbalanceAgainst = true;
    }
    if (imbalanceAgainst) {
        exitTrade("IMBALANCE_FLIP", snap);
        return;
    }
    
    // 4. Latency degradation
    if (!snap.latencyStable) {
        exitTrade("LATENCY_DEGRADED", snap);
        return;
    }
}

void StopRunFadeProfile::exitTrade(const char* reason, const SRMarketSnapshot& snap) {
    // Close the position
    closePosition(reason);
    
    // Calculate PnL
    double exitPrice = (positionSide_ == SRSide::BUY) ? snap.bid : snap.ask;
    double pnlBps = 0.0;
    if (entryPrice_ > 0) {
        pnlBps = (exitPrice - entryPrice_) / entryPrice_ * 10000.0;
        if (positionSide_ == SRSide::SELL) pnlBps = -pnlBps;
    }
    
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    double heldMs = static_cast<double>(heldNs) / 1e6;
    
    printf("[STOP_RUN] EXIT %s: %s @ %.5f | PnL: %.2f bps | Held: %.0f ms\n",
           currentSymbol_.c_str(), reason, exitPrice, pnlBps, heldMs);
    
    // Update loss tracking
    if (pnlBps < -0.05) {
        lossVelocity_.recordLoss(snap.now_ns);
    }
    
    // Reset position state
    hasPosition_ = false;
    positionSide_ = SRSide::NONE;
    entryPrice_ = 0.0;
    entryVwap_ = 0.0;
    lastTradeEndNs_ = snap.now_ns;
    
    // Enter cooldown
    state_ = SRState::COOLDOWN;
    stateTs_ns_ = snap.now_ns;
    idleReason_ = SRIdleReason::COOLDOWN_ACTIVE;
}

void StopRunFadeProfile::onFill(double fillPrice, double qty, bool isBuy) {
    printf("[STOP_RUN] FILL: %s %.6f @ %.5f\n",
           isBuy ? "BUY" : "SELL", qty, fillPrice);
}

void StopRunFadeProfile::onPositionClosed(double pnl_bps, uint64_t heldNs) {
    (void)pnl_bps;
    (void)heldNs;
}

void StopRunFadeProfile::onTimer(uint64_t now_ns) {
    (void)now_ns;
}

void StopRunFadeProfile::printStatus() const {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  STOP RUN FADE STATUS                                       ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  State:     %-15s                                     ║\n", srStateToString(state_));
    printf("║  Idle:      %s %-20s                          ║\n",
           srIdleReasonIcon(idleReason_), srIdleReasonToString(idleReason_));
    printf("║  Session:   %-10s (%.1fx risk, %d/%d trades)           ║\n",
           currentSession_.c_str(), sessionPolicy_.riskMultiplier,
           tradesThisSession_, sessionPolicy_.maxTradesPerSession);
    printf("║  Position:  %s                                              ║\n",
           hasPosition_ ? "YES" : "NO");
    printf("║  Enabled:   %s                                              ║\n",
           enabled_ ? "YES" : "NO");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void StopRunFadeProfile::toJSON(char* buf, size_t buf_size) const {
    snprintf(buf, buf_size,
        "{"
        "\"profile\":\"STOP_RUN_FADE\","
        "\"state\":\"%s\","
        "\"idle_reason\":\"%s\","
        "\"session\":\"%s\","
        "\"risk_mult\":%.2f,"
        "\"trades_session\":%d,"
        "\"max_trades\":%d,"
        "\"has_position\":%s,"
        "\"enabled\":%s"
        "}",
        srStateToString(state_),
        srIdleReasonToString(idleReason_),
        currentSession_.c_str(),
        sessionPolicy_.riskMultiplier,
        tradesThisSession_,
        sessionPolicy_.maxTradesPerSession,
        hasPosition_ ? "true" : "false",
        enabled_.load() ? "true" : "false"
    );
}

} // namespace Chimera
