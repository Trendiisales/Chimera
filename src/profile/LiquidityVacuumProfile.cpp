// =============================================================================
// LiquidityVacuumProfile.cpp - v4.9.0 - LIQUIDITY VACUUM IMPLEMENTATION
// =============================================================================
#include "profile/LiquidityVacuumProfile.hpp"

#include <cstring>
#include <cmath>

namespace Chimera {

LiquidityVacuumProfile::LiquidityVacuumProfile()
    : state_(LVState::IDLE),
      idleReason_(LVIdleReason::NONE),
      stateTs_ns_(0),
      hasPosition_(false),
      positionSide_(LVSide::NONE),
      entryPrice_(0.0),
      peakPrice_(0.0),
      tradeStartNs_(0),
      tradesThisSession_(0),
      enabled_(true)
{
}

void LiquidityVacuumProfile::resetSession() {
    tradesThisSession_ = 0;
    currentSession_.clear();
    sessionPolicy_ = LVSessionPolicy();
    vwapState_ = VwapState();
    vacuums_.clear();
    jumpTrackers_.clear();
    
    state_ = LVState::IDLE;
    idleReason_ = LVIdleReason::NONE;
    
    printf("[LIQ_VAC] Session reset complete\n");
}

void LiquidityVacuumProfile::onTick(const LVMarketSnapshot& snap) {
    if (!enabled_) {
        idleReason_ = LVIdleReason::GO_NO_GO_BLOCK;
        if (state_ != LVState::IN_TRADE) {
            state_ = LVState::IDLE;
        }
        return;
    }
    
    // Update session policy if session changed
    if (currentSession_ != snap.currentSession) {
        currentSession_ = snap.currentSession;
        sessionPolicy_ = getLVSessionPolicy(currentSession_);
        tradesThisSession_ = 0;
    }
    
    // Update VWAP state
    vwapState_.update(snap.vwapSlope);
    
    // Update price tracker
    jumpTrackers_[snap.symbol].addTick(snap.mid, snap.now_ns);
    
    // Check hard gates
    if (!hardGatesPass(snap)) {
        if (state_ != LVState::IN_TRADE) {
            state_ = LVState::IDLE;
        }
        return;
    }
    
    // State machine
    switch (state_) {
        case LVState::IDLE:
            if (detectVacuum(snap)) {
                state_ = LVState::VACUUM_DETECTED;
                stateTs_ns_ = snap.now_ns;
                idleReason_ = LVIdleReason::WAITING_CONFIRMATION;
                
                auto& vac = vacuums_[snap.symbol];
                printf("[LIQ_VAC] Vacuum detected %s dir=%d ticks=%d depth_ratio=%.2f\n",
                       snap.symbol, vac.direction, vac.ticksMoved, snap.depthRatio());
            } else {
                idleReason_ = LVIdleReason::NO_VACUUM_DETECTED;
            }
            break;
            
        case LVState::VACUUM_DETECTED:
            evaluateEntry(snap);
            break;
            
        case LVState::CONFIRM_CONTINUATION:
            // Brief state - immediately enter
            {
                auto& vac = vacuums_[snap.symbol];
                LVSide side = (vac.direction > 0) ? LVSide::BUY : LVSide::SELL;
                enterTrade(side, snap);
            }
            break;
            
        case LVState::IN_TRADE:
            evaluateExit(snap);
            break;
            
        case LVState::DONE: {
            auto cfg = getLVConfig(snap.symbol);
            if (snap.now_ns - stateTs_ns_ > cfg.cooldownNs) {
                state_ = LVState::IDLE;
                idleReason_ = LVIdleReason::NONE;
                vacuums_[snap.symbol].reset();
            } else {
                idleReason_ = LVIdleReason::COOLDOWN_ACTIVE;
            }
            break;
        }
    }
}

bool LiquidityVacuumProfile::hardGatesPass(const LVMarketSnapshot& snap) {
    // 1. GoNoGo gate
    if (!snap.goNoGoIsGo) {
        idleReason_ = LVIdleReason::GO_NO_GO_BLOCK;
        return false;
    }
    
    // 2. Latency
    if (!snap.latencyStable) {
        idleReason_ = LVIdleReason::LATENCY_UNSTABLE;
        return false;
    }
    
    // 3. Session enabled
    if (!sessionPolicy_.isEnabled()) {
        idleReason_ = LVIdleReason::SESSION_DISABLED;
        return false;
    }
    
    // 4. Shock
    if (snap.shockActive) {
        idleReason_ = LVIdleReason::SHOCK_ACTIVE;
        return false;
    }
    
    // 5. Symbol enabled
    if (!isLVSymbolEnabled(snap.symbol)) {
        idleReason_ = LVIdleReason::SYMBOL_DISABLED;
        return false;
    }
    
    // 6. Trade limit
    if (tradesThisSession_ >= sessionPolicy_.maxTradesPerSession) {
        idleReason_ = LVIdleReason::TRADE_LIMIT_REACHED;
        return false;
    }
    
    // 7. Already have position
    if (hasPosition_) {
        idleReason_ = LVIdleReason::POSITION_OPEN;
        return true;  // Allow exit evaluation
    }
    
    return true;
}

bool LiquidityVacuumProfile::detectVacuum(const LVMarketSnapshot& snap) {
    auto cfg = getLVConfig(snap.symbol);
    auto& vac = vacuums_[snap.symbol];
    auto& tracker = jumpTrackers_[snap.symbol];
    
    // Check depth drop
    double depthRatio = snap.depthRatio();
    if (depthRatio > (1.0 - cfg.depthDropPct)) {
        // Depth hasn't dropped enough
        return false;
    }
    
    // Check spread not widening abnormally (distinguishes from news)
    double spreadRatio = snap.spreadRatio();
    if (spreadRatio > cfg.maxSpreadMult) {
        idleReason_ = LVIdleReason::SPREAD_TOO_WIDE;
        return false;
    }
    
    // Check price jump
    int ticksMoved = tracker.getTicksMoved(cfg.jumpWindowNs, snap.now_ns, cfg.tickSize);
    if (ticksMoved < cfg.minJumpTicks) {
        return false;
    }
    
    // Determine direction
    double priceChange = tracker.getPriceChange(cfg.jumpWindowNs, snap.now_ns);
    int direction = (priceChange > 0) ? +1 : -1;
    
    // Vacuum detected!
    vac.detected = true;
    vac.direction = direction;
    vac.jumpStartPrice = tracker.getStartPrice(cfg.jumpWindowNs, snap.now_ns);
    vac.jumpPeakPrice = snap.mid;
    vac.jumpStartNs = snap.now_ns;
    vac.depthAtJump = snap.depthRatio();
    vac.spreadAtJump = snap.spreadRatio();
    vac.ticksMoved = ticksMoved;
    
    return true;
}

void LiquidityVacuumProfile::evaluateEntry(const LVMarketSnapshot& snap) {
    auto& vac = vacuums_[snap.symbol];
    auto cfg = getLVConfig(snap.symbol);
    
    if (!vac.detected) {
        state_ = LVState::IDLE;
        idleReason_ = LVIdleReason::NO_VACUUM_DETECTED;
        return;
    }
    
    // Start confirmation window
    if (!vac.confirmationStarted) {
        vac.confirmationStarted = true;
        vac.confirmStartNs = snap.now_ns;
    }
    
    uint64_t timeSinceConfirmStart = snap.now_ns - vac.confirmStartNs;
    
    // Check confirmation
    if (confirmContinuation(snap)) {
        state_ = LVState::CONFIRM_CONTINUATION;
        stateTs_ns_ = snap.now_ns;
        
        printf("[LIQ_VAC] Continuation confirmed %s\n", snap.symbol);
        return;
    }
    
    // Check for confirmation failure
    if (timeSinceConfirmStart > cfg.confirmWindowNs) {
        // Confirmation window expired without confirmation
        vac.reset();
        state_ = LVState::IDLE;
        idleReason_ = LVIdleReason::CONFIRMATION_FAILED;
        
        printf("[LIQ_VAC] Confirmation failed %s - no continuation\n", snap.symbol);
        return;
    }
    
    idleReason_ = LVIdleReason::WAITING_CONFIRMATION;
}

bool LiquidityVacuumProfile::confirmContinuation(const LVMarketSnapshot& snap) {
    auto& vac = vacuums_[snap.symbol];
    
    if (!vac.detected || !vac.confirmationStarted) return false;
    
    // Check continuation in same direction
    bool continuing = false;
    if (vac.direction > 0) {
        // Up vacuum - price should continue up
        continuing = (snap.mid > vac.jumpPeakPrice);
        vac.jumpPeakPrice = std::max(vac.jumpPeakPrice, snap.mid);
    } else {
        // Down vacuum - price should continue down
        continuing = (snap.mid < vac.jumpPeakPrice);
        vac.jumpPeakPrice = std::min(vac.jumpPeakPrice, snap.mid);
    }
    
    if (!continuing) return false;
    
    // Check VWAP slope alignment
    bool vwapAligned = false;
    if (vac.direction > 0 && vwapState_.direction != VwapDirection::DOWN) {
        vwapAligned = true;
    }
    if (vac.direction < 0 && vwapState_.direction != VwapDirection::UP) {
        vwapAligned = true;
    }
    
    return continuing && vwapAligned;
}

void LiquidityVacuumProfile::enterTrade(LVSide side, const LVMarketSnapshot& snap) {
    // Calculate position size (0.05% risk)
    double riskPct = 0.0005;
    double adjustedRisk = riskPct * sessionPolicy_.riskMultiplier;
    double qty = calculateQty(adjustedRisk, snap);
    
    // Submit order
    submitOrder(side, qty, snap.symbol);
    
    // Update state
    hasPosition_ = true;
    positionSide_ = side;
    entryPrice_ = (side == LVSide::BUY) ? snap.ask : snap.bid;
    peakPrice_ = entryPrice_;
    tradeStartNs_ = snap.now_ns;
    currentSymbol_ = snap.symbol;
    tradesThisSession_++;
    
    state_ = LVState::IN_TRADE;
    idleReason_ = LVIdleReason::NONE;
    
    auto& vac = vacuums_[snap.symbol];
    printf("[LIQ_VAC] ENTRY %s %s @ %.5f | Ticks: %d | Depth: %.2f | Trade #%d\n",
           lvSideToString(side), snap.symbol, entryPrice_,
           vac.ticksMoved, snap.depthRatio(), tradesThisSession_);
}

void LiquidityVacuumProfile::evaluateExit(const LVMarketSnapshot& snap) {
    if (!hasPosition_) return;
    if (currentSymbol_ != snap.symbol) return;
    
    auto cfg = getLVConfig(currentSymbol_);
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    // Track peak price
    if (positionSide_ == LVSide::BUY) {
        peakPrice_ = std::max(peakPrice_, snap.mid);
    } else {
        peakPrice_ = std::min(peakPrice_, snap.mid);
    }
    
    // Exit triggers (FIRST HIT WINS)
    
    // 1. Time cap (1.0-1.5s) - THIS ENGINE NEVER HOLDS
    if (heldNs > cfg.maxHoldNs) {
        exitTrade("TIME_CAP", snap);
        return;
    }
    
    // 2. Continuation stalled (price retreated from peak)
    double retreatPct = 0.0;
    if (positionSide_ == LVSide::BUY && peakPrice_ > entryPrice_) {
        retreatPct = (peakPrice_ - snap.mid) / (peakPrice_ - entryPrice_);
    } else if (positionSide_ == LVSide::SELL && peakPrice_ < entryPrice_) {
        retreatPct = (snap.mid - peakPrice_) / (entryPrice_ - peakPrice_);
    }
    
    if (retreatPct > 0.5) {  // Lost 50% of gains
        exitTrade("CONTINUATION_STALLED", snap);
        return;
    }
    
    // 3. VWAP rejection
    bool vwapRejection = false;
    if (positionSide_ == LVSide::BUY && vwapState_.direction == VwapDirection::DOWN) {
        vwapRejection = true;
    }
    if (positionSide_ == LVSide::SELL && vwapState_.direction == VwapDirection::UP) {
        vwapRejection = true;
    }
    if (vwapRejection) {
        exitTrade("VWAP_REJECTION", snap);
        return;
    }
    
    // 4. Latency spike
    if (!snap.latencyStable) {
        exitTrade("LATENCY_SPIKE", snap);
        return;
    }
}

void LiquidityVacuumProfile::exitTrade(const char* reason, const LVMarketSnapshot& snap) {
    closePosition(reason);
    
    double exitPrice = (positionSide_ == LVSide::BUY) ? snap.bid : snap.ask;
    double pnlBps = 0.0;
    if (entryPrice_ > 0) {
        pnlBps = (exitPrice - entryPrice_) / entryPrice_ * 10000.0;
        if (positionSide_ == LVSide::SELL) pnlBps = -pnlBps;
    }
    
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    double heldMs = static_cast<double>(heldNs) / 1e6;
    
    printf("[LIQ_VAC] EXIT %s: %s @ %.5f | PnL: %.2f bps | Held: %.0f ms\n",
           currentSymbol_.c_str(), reason, exitPrice, pnlBps, heldMs);
    
    // Reset position state
    hasPosition_ = false;
    positionSide_ = LVSide::NONE;
    entryPrice_ = 0.0;
    peakPrice_ = 0.0;
    
    // Enter done/cooldown state
    state_ = LVState::DONE;
    stateTs_ns_ = snap.now_ns;
    idleReason_ = LVIdleReason::COOLDOWN_ACTIVE;
}

void LiquidityVacuumProfile::onFill(double fillPrice, double qty, bool isBuy) {
    printf("[LIQ_VAC] FILL: %s %.6f @ %.5f\n",
           isBuy ? "BUY" : "SELL", qty, fillPrice);
}

void LiquidityVacuumProfile::onPositionClosed(double pnl_bps, uint64_t heldNs) {
    (void)pnl_bps;
    (void)heldNs;
}

void LiquidityVacuumProfile::onTimer(uint64_t now_ns) {
    (void)now_ns;
}

void LiquidityVacuumProfile::printStatus() const {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  LIQUIDITY VACUUM STATUS                                     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  State:     %-15s                                     ║\n", lvStateToString(state_));
    printf("║  Idle:      %s %-20s                          ║\n",
           lvIdleReasonIcon(idleReason_), lvIdleReasonToString(idleReason_));
    printf("║  Session:   %-10s (%.1fx risk, %d/%d trades)           ║\n",
           currentSession_.c_str(), sessionPolicy_.riskMultiplier,
           tradesThisSession_, sessionPolicy_.maxTradesPerSession);
    printf("║  Position:  %s                                              ║\n",
           hasPosition_ ? "YES" : "NO");
    printf("║  Enabled:   %s                                              ║\n",
           enabled_ ? "YES" : "NO");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void LiquidityVacuumProfile::toJSON(char* buf, size_t buf_size) const {
    snprintf(buf, buf_size,
        "{"
        "\"profile\":\"LIQUIDITY_VACUUM\","
        "\"state\":\"%s\","
        "\"idle_reason\":\"%s\","
        "\"session\":\"%s\","
        "\"risk_mult\":%.2f,"
        "\"trades_session\":%d,"
        "\"max_trades\":%d,"
        "\"has_position\":%s,"
        "\"enabled\":%s"
        "}",
        lvStateToString(state_),
        lvIdleReasonToString(idleReason_),
        currentSession_.c_str(),
        sessionPolicy_.riskMultiplier,
        tradesThisSession_,
        sessionPolicy_.maxTradesPerSession,
        hasPosition_ ? "true" : "false",
        enabled_.load() ? "true" : "false"
    );
}

} // namespace Chimera
