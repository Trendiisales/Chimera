// =============================================================================
// OpenRangeProfile.cpp - v4.9.0 - OPEN RANGE EXPLOITER IMPLEMENTATION
// =============================================================================
#include "profile/OpenRangeProfile.hpp"

#include <cstring>
#include <cmath>
#include <chrono>

namespace Chimera {

// Static empty range for const reference
OpeningRange OpenRangeProfile::emptyRange_;

OpenRangeProfile::OpenRangeProfile()
    : state_(ORState::IDLE),
      idleReason_(ORIdleReason::NONE),
      stateTs_ns_(0),
      hasPosition_(false),
      positionSide_(ORSide::NONE),
      entryPrice_(0.0),
      tradeStartNs_(0),
      tradesThisDay_(0),
      enabled_(true)
{
}

void OpenRangeProfile::resetDay() {
    tradesThisDay_ = 0;
    tradedToday_.clear();
    ranges_.clear();
    breaks_.clear();
    state_ = ORState::IDLE;
    idleReason_ = ORIdleReason::NONE;
    vwapState_ = VwapState();
    hasPosition_ = false;
    positionSide_ = ORSide::NONE;
    entryPrice_ = 0.0;
    currentSymbol_.clear();
    
    printf("[ORE] Day reset complete\n");
}

const OpeningRange& OpenRangeProfile::getRange(const char* symbol) const {
    auto it = ranges_.find(symbol);
    if (it == ranges_.end()) return emptyRange_;
    return it->second;
}

bool OpenRangeProfile::isNYOpenWindow(uint64_t now_ns) const {
    (void)now_ns;  // Use wall clock time instead
    
    auto now_tp = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now_tp);
    std::tm* utc_tm = std::gmtime(&now_time);
    
    int hour = utc_tm->tm_hour;
    int min = utc_tm->tm_min;
    
    // NY Open window: 13:30-13:35 UTC (09:30-09:35 EST)
    if (hour == 13 && min >= 30 && min < 35) {
        return true;
    }
    return false;
}

void OpenRangeProfile::onTick(const ORMarketSnapshot& snap) {
    // Check if enabled
    if (!enabled_) {
        idleReason_ = ORIdleReason::GO_NO_GO_BLOCK;
        if (state_ != ORState::IN_TRADE) {
            state_ = ORState::IDLE;
        }
        return;
    }
    
    // Update VWAP state
    vwapState_.update(snap.vwapSlope);
    
    // Check hard gates
    if (!hardGatesPass(snap)) {
        if (state_ != ORState::IN_TRADE) {
            // Don't reset to IDLE if we're building range or armed
            if (state_ != ORState::RANGE_BUILDING && state_ != ORState::ARMED) {
                state_ = ORState::IDLE;
            }
        }
        return;
    }
    
    // State machine
    switch (state_) {
        case ORState::IDLE:
            // Check if we should start building range
            if (snap.isNYOpenWindow) {
                // Check if already traded this symbol today
                if (tradedToday_.count(snap.symbol) > 0) {
                    idleReason_ = ORIdleReason::ALREADY_TRADED_TODAY;
                    return;
                }
                
                // Start building range
                auto& range = ranges_[snap.symbol];
                range.reset();
                range.buildStartNs = snap.now_ns;
                range.open = snap.mid;
                range.high = snap.mid;
                range.low = snap.mid;
                range.vwap = snap.vwap;
                
                breaks_[snap.symbol].reset();
                
                state_ = ORState::RANGE_BUILDING;
                stateTs_ns_ = snap.now_ns;
                idleReason_ = ORIdleReason::RANGE_NOT_BUILT;
                
                printf("[ORE] Started building range for %s @ %.2f\n",
                       snap.symbol, snap.mid);
            } else {
                idleReason_ = ORIdleReason::NOT_NY_OPEN_WINDOW;
            }
            break;
            
        case ORState::RANGE_BUILDING:
            buildRange(snap);
            break;
            
        case ORState::ARMED:
            evaluateEntry(snap);
            break;
            
        case ORState::IN_TRADE:
            evaluateExit(snap);
            break;
            
        case ORState::DONE:
            // Stay in DONE until day reset
            idleReason_ = ORIdleReason::ALREADY_TRADED_TODAY;
            break;
    }
}

bool OpenRangeProfile::hardGatesPass(const ORMarketSnapshot& snap) {
    // 1. GoNoGo gate
    if (!snap.goNoGoIsGo) {
        idleReason_ = ORIdleReason::GO_NO_GO_BLOCK;
        return false;
    }
    
    // 2. Latency
    if (!snap.latencyStable) {
        idleReason_ = ORIdleReason::LATENCY_UNSTABLE;
        return false;
    }
    
    // 3. Shock
    if (snap.shockActive) {
        idleReason_ = ORIdleReason::SHOCK_ACTIVE;
        return false;
    }
    
    // 4. Symbol enabled
    if (!isORSymbolEnabled(snap.symbol)) {
        idleReason_ = ORIdleReason::SYMBOL_DISABLED;
        return false;
    }
    
    // 5. Already traded this symbol today
    if (tradedToday_.count(snap.symbol) > 0 && state_ != ORState::IN_TRADE) {
        idleReason_ = ORIdleReason::ALREADY_TRADED_TODAY;
        return false;
    }
    
    // 6. Already have position in different symbol
    if (hasPosition_ && currentSymbol_ != snap.symbol) {
        idleReason_ = ORIdleReason::POSITION_OPEN;
        return true;  // Allow exit evaluation for current position
    }
    
    return true;
}

void OpenRangeProfile::buildRange(const ORMarketSnapshot& snap) {
    auto& range = ranges_[snap.symbol];
    auto cfg = getORConfig(snap.symbol);
    
    // Update range high/low
    range.high = std::max(range.high, snap.mid);
    range.low = std::min(range.low, snap.mid);
    
    // Check if 2 minutes have passed
    uint64_t elapsed = snap.now_ns - range.buildStartNs;
    if (elapsed >= RANGE_BUILD_DURATION_NS) {
        // Validate range size
        double rangeSize = range.range();
        
        if (rangeSize >= cfg.minRangePoints && rangeSize <= cfg.maxRangePoints) {
            range.buildEndNs = snap.now_ns;
            range.vwap = snap.vwap;
            range.isValid = true;
            
            state_ = ORState::ARMED;
            stateTs_ns_ = snap.now_ns;
            idleReason_ = ORIdleReason::NO_BREAK_DETECTED;
            
            printf("[ORE] Range built for %s: High=%.2f Low=%.2f Range=%.2f VWAP=%.2f\n",
                   snap.symbol, range.high, range.low, rangeSize, range.vwap);
        } else {
            // Invalid range - go back to IDLE
            printf("[ORE] Invalid range for %s (size=%.2f, min=%.2f, max=%.2f) - waiting for next window\n",
                   snap.symbol, rangeSize, cfg.minRangePoints, cfg.maxRangePoints);
            range.reset();
            state_ = ORState::IDLE;
            idleReason_ = ORIdleReason::NOT_NY_OPEN_WINDOW;
        }
    }
}

void OpenRangeProfile::evaluateEntry(const ORMarketSnapshot& snap) {
    auto& range = ranges_[snap.symbol];
    auto& brk = breaks_[snap.symbol];
    auto cfg = getORConfig(snap.symbol);
    
    // Detect break if not already detected
    if (!brk.breakDetected) {
        if (detectBreak(snap)) {
            // Break detected, start tracking
            idleReason_ = ORIdleReason::WAITING_ACCEPTANCE;
        } else {
            idleReason_ = ORIdleReason::NO_BREAK_DETECTED;
        }
        return;
    }
    
    // We have a break, check for acceptance or rejection
    uint64_t timeSinceBreak = snap.now_ns - brk.breakTimestampNs;
    double timeSinceBreakSec = static_cast<double>(timeSinceBreak) / 1e9;
    
    // Check for acceptance (held outside range for required time)
    if (!brk.acceptanceConfirmed && !brk.rejectionConfirmed) {
        if (checkAcceptance(snap)) {
            brk.acceptanceConfirmed = true;
            
            // Entry Type A: Range Break + Acceptance
            ORSide side = (brk.breakDirection > 0) ? ORSide::BUY : ORSide::SELL;
            
            // Confirm with imbalance
            bool imbalanceConfirms = (brk.breakDirection > 0 && snap.imbalance > cfg.minImbalance) ||
                                     (brk.breakDirection < 0 && snap.imbalance < -cfg.minImbalance);
            
            // Confirm with VWAP slope
            bool vwapConfirms = (brk.breakDirection > 0 && vwapState_.direction == VwapDirection::UP) ||
                                (brk.breakDirection < 0 && vwapState_.direction == VwapDirection::DOWN);
            
            if (imbalanceConfirms && vwapConfirms) {
                enterTrade(side, snap, "BREAK_ACCEPTANCE");
                return;
            } else {
                printf("[ORE] Break accepted but conditions not confirmed (imb=%.2f vwap=%s) - no trade\n",
                       snap.imbalance, vwapDirectionToString(vwapState_.direction));
                brk.reset();
                idleReason_ = ORIdleReason::NO_BREAK_DETECTED;
            }
        }
        
        // Check for rejection (price returned inside range quickly)
        if (checkRejection(snap)) {
            brk.rejectionConfirmed = true;
            
            // Entry Type B: Range Failure Fade
            // Enter AGAINST the failed break
            ORSide side = (brk.breakDirection > 0) ? ORSide::SELL : ORSide::BUY;
            
            // For fade, imbalance should have flipped
            bool imbalanceFlipped = (brk.breakDirection > 0 && snap.imbalance < -0.3) ||
                                    (brk.breakDirection < 0 && snap.imbalance > 0.3);
            
            // VWAP should reject (go opposite direction)
            bool vwapRejects = (brk.breakDirection > 0 && vwapState_.direction != VwapDirection::UP) ||
                               (brk.breakDirection < 0 && vwapState_.direction != VwapDirection::DOWN);
            
            if (imbalanceFlipped && vwapRejects) {
                enterTrade(side, snap, "BREAK_REJECTION_FADE");
                return;
            } else {
                printf("[ORE] Break rejected but fade conditions not met - no trade\n");
                brk.reset();
                idleReason_ = ORIdleReason::NO_BREAK_DETECTED;
            }
        }
    }
    
    // If break timed out without acceptance or rejection, reset
    if (timeSinceBreakSec > 5.0) {
        printf("[ORE] Break timed out for %s - resetting\n", snap.symbol);
        brk.reset();
        idleReason_ = ORIdleReason::NO_BREAK_DETECTED;
    }
}

bool OpenRangeProfile::detectBreak(const ORMarketSnapshot& snap) {
    auto& range = ranges_[snap.symbol];
    auto& brk = breaks_[snap.symbol];
    
    if (!range.isValid) return false;
    
    // Check for break above high
    if (snap.mid > range.high) {
        brk.breakDetected = true;
        brk.breakDirection = +1;
        brk.breakPrice = snap.mid;
        brk.breakTimestampNs = snap.now_ns;
        
        printf("[ORE] BREAK HIGH detected for %s @ %.2f (range high=%.2f)\n",
               snap.symbol, snap.mid, range.high);
        return true;
    }
    
    // Check for break below low
    if (snap.mid < range.low) {
        brk.breakDetected = true;
        brk.breakDirection = -1;
        brk.breakPrice = snap.mid;
        brk.breakTimestampNs = snap.now_ns;
        
        printf("[ORE] BREAK LOW detected for %s @ %.2f (range low=%.2f)\n",
               snap.symbol, snap.mid, range.low);
        return true;
    }
    
    return false;
}

bool OpenRangeProfile::checkAcceptance(const ORMarketSnapshot& snap) {
    auto& range = ranges_[snap.symbol];
    auto& brk = breaks_[snap.symbol];
    auto cfg = getORConfig(snap.symbol);
    
    if (!brk.breakDetected) return false;
    
    uint64_t timeSinceBreak = snap.now_ns - brk.breakTimestampNs;
    double timeSinceBreakSec = static_cast<double>(timeSinceBreak) / 1e9;
    
    // Must hold outside range for acceptance duration
    if (timeSinceBreakSec < cfg.acceptanceHoldSec) {
        return false;
    }
    
    // Price must still be outside range
    if (brk.breakDirection > 0 && snap.mid > range.high) {
        return true;  // Still above high after hold period
    }
    if (brk.breakDirection < 0 && snap.mid < range.low) {
        return true;  // Still below low after hold period
    }
    
    return false;
}

bool OpenRangeProfile::checkRejection(const ORMarketSnapshot& snap) {
    auto& range = ranges_[snap.symbol];
    auto& brk = breaks_[snap.symbol];
    auto cfg = getORConfig(snap.symbol);
    
    if (!brk.breakDetected) return false;
    
    uint64_t timeSinceBreak = snap.now_ns - brk.breakTimestampNs;
    double timeSinceBreakSec = static_cast<double>(timeSinceBreak) / 1e9;
    
    // Must fail within rejection time window
    if (timeSinceBreakSec > cfg.rejectionTimeSec) {
        return false;
    }
    
    // Price must have returned inside range
    if (snap.mid >= range.low && snap.mid <= range.high) {
        return true;  // Back inside range = rejection
    }
    
    return false;
}

void OpenRangeProfile::enterTrade(ORSide side, const ORMarketSnapshot& snap, const char* reason) {
    // Calculate position size (0.15% risk)
    double riskPct = 0.0015;
    double qty = calculateQty(riskPct, snap);
    
    // Submit order
    submitOrder(side, qty, snap.symbol);
    
    // Update state
    hasPosition_ = true;
    positionSide_ = side;
    entryPrice_ = (side == ORSide::BUY) ? snap.ask : snap.bid;
    tradeStartNs_ = snap.now_ns;
    currentSymbol_ = snap.symbol;
    tradesThisDay_++;
    tradedToday_.insert(snap.symbol);
    
    state_ = ORState::IN_TRADE;
    idleReason_ = ORIdleReason::NONE;
    
    auto& range = ranges_[snap.symbol];
    printf("[ORE] ENTRY %s %s @ %.5f | Reason: %s | Range: [%.2f-%.2f]\n",
           orSideToString(side), snap.symbol, entryPrice_, reason,
           range.low, range.high);
}

void OpenRangeProfile::evaluateExit(const ORMarketSnapshot& snap) {
    if (!hasPosition_) return;
    if (currentSymbol_ != snap.symbol) return;
    
    auto cfg = getORConfig(currentSymbol_);
    auto& range = ranges_[currentSymbol_];
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    // Exit triggers (FIRST HIT WINS)
    
    // 1. Hard time cap (20s default)
    if (heldNs > cfg.maxHoldNs) {
        exitTrade("TIME_CAP", snap);
        return;
    }
    
    // 2. VWAP reclaim against position
    if (positionSide_ == ORSide::BUY && snap.mid < range.vwap) {
        exitTrade("VWAP_RECLAIM_AGAINST", snap);
        return;
    }
    if (positionSide_ == ORSide::SELL && snap.mid > range.vwap) {
        exitTrade("VWAP_RECLAIM_AGAINST", snap);
        return;
    }
    
    // 3. Edge decay - price moving against us past range midpoint
    double midpoint = range.midpoint();
    if (positionSide_ == ORSide::BUY && snap.mid < midpoint) {
        exitTrade("EDGE_DECAY", snap);
        return;
    }
    if (positionSide_ == ORSide::SELL && snap.mid > midpoint) {
        exitTrade("EDGE_DECAY", snap);
        return;
    }
    
    // 4. Latency degradation
    if (!snap.latencyStable) {
        exitTrade("LATENCY_DEGRADED", snap);
        return;
    }
}

void OpenRangeProfile::exitTrade(const char* reason, const ORMarketSnapshot& snap) {
    // Close the position
    closePosition(reason);
    
    // Calculate PnL
    double exitPrice = (positionSide_ == ORSide::BUY) ? snap.bid : snap.ask;
    double pnlBps = 0.0;
    if (entryPrice_ > 0) {
        pnlBps = (exitPrice - entryPrice_) / entryPrice_ * 10000.0;
        if (positionSide_ == ORSide::SELL) pnlBps = -pnlBps;
    }
    
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    double heldMs = static_cast<double>(heldNs) / 1e6;
    
    printf("[ORE] EXIT %s: %s @ %.5f | PnL: %.2f bps | Held: %.0f ms\n",
           currentSymbol_.c_str(), reason, exitPrice, pnlBps, heldMs);
    
    // Reset position state
    hasPosition_ = false;
    positionSide_ = ORSide::NONE;
    entryPrice_ = 0.0;
    
    // Mark as DONE for this symbol (1 trade per symbol per day)
    state_ = ORState::DONE;
    stateTs_ns_ = snap.now_ns;
    idleReason_ = ORIdleReason::ALREADY_TRADED_TODAY;
}

void OpenRangeProfile::onFill(double fillPrice, double qty, bool isBuy) {
    printf("[ORE] FILL: %s %.6f @ %.5f\n",
           isBuy ? "BUY" : "SELL", qty, fillPrice);
}

void OpenRangeProfile::onPositionClosed(double pnl_bps, uint64_t heldNs) {
    (void)pnl_bps;
    (void)heldNs;
    // External notification handled
}

void OpenRangeProfile::onTimer(uint64_t now_ns) {
    (void)now_ns;
}

void OpenRangeProfile::printStatus() const {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  OPEN RANGE EXPLOITER (ORE) STATUS                          ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  State:     %-15s                                     ║\n", orStateToString(state_));
    printf("║  Idle:      %s %-20s                          ║\n",
           orIdleReasonIcon(idleReason_), orIdleReasonToString(idleReason_));
    printf("║  Position:  %s                                              ║\n",
           hasPosition_ ? "YES" : "NO");
    printf("║  Trades:    %d/day                                           ║\n", tradesThisDay_);
    printf("║  Enabled:   %s                                              ║\n",
           enabled_ ? "YES" : "NO");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  RANGES:                                                     ║\n");
    for (const auto& [symbol, range] : ranges_) {
        if (range.isValid) {
            printf("║    %s: [%.2f - %.2f] (%.2f pts)                          ║\n",
                   symbol.c_str(), range.low, range.high, range.range());
        }
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void OpenRangeProfile::toJSON(char* buf, size_t buf_size) const {
    snprintf(buf, buf_size,
        "{"
        "\"profile\":\"OPEN_RANGE\","
        "\"state\":\"%s\","
        "\"idle_reason\":\"%s\","
        "\"trades_day\":%d,"
        "\"has_position\":%s,"
        "\"enabled\":%s"
        "}",
        orStateToString(state_),
        orIdleReasonToString(idleReason_),
        tradesThisDay_,
        hasPosition_ ? "true" : "false",
        enabled_.load() ? "true" : "false"
    );
}

} // namespace Chimera
