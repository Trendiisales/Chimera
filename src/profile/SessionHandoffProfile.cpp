// =============================================================================
// SessionHandoffProfile.cpp - v4.9.0 - SESSION HANDOFF IMPLEMENTATION
// =============================================================================
#include "profile/SessionHandoffProfile.hpp"

#include <cstring>
#include <cmath>
#include <chrono>

namespace Chimera {

SessionHandoffProfile::SessionHandoffProfile()
    : state_(SHState::IDLE),
      idleReason_(SHIdleReason::NONE),
      stateTs_ns_(0),
      currentHandoff_(HandoffType::NONE),
      hasPosition_(false),
      positionSide_(SHSide::NONE),
      entryPrice_(0.0),
      entryVwap_(0.0),
      tradeStartNs_(0),
      tradesThisDay_(0),
      enabled_(true)
{
}

void SessionHandoffProfile::resetDay() {
    tradesThisDay_ = 0;
    tradedHandoffs_.clear();
    priorAnalysis_.clear();
    biasAnalysis_.reset();
    currentHandoff_ = HandoffType::NONE;
    observingSession_.clear();
    targetSession_.clear();
    lastSession_.clear();
    
    state_ = SHState::IDLE;
    idleReason_ = SHIdleReason::NONE;
    vwapState_ = VwapState();
    
    hasPosition_ = false;
    positionSide_ = SHSide::NONE;
    entryPrice_ = 0.0;
    entryVwap_ = 0.0;
    currentSymbol_.clear();
    
    printf("[SESSION_HO] Day reset complete\n");
}

HandoffType SessionHandoffProfile::detectHandoffWindow(const SHMarketSnapshot& snap) {
    (void)snap;
    
    auto now_tp = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now_tp);
    std::tm* utc_tm = std::gmtime(&now_time);
    
    int hour = utc_tm->tm_hour;
    int min = utc_tm->tm_min;
    int total_mins = hour * 60 + min;
    
    // Asia→London handoff: 06:45-07:15 UTC (405-435 minutes)
    if (total_mins >= 405 && total_mins < 435) {
        return HandoffType::ASIA_TO_LONDON;
    }
    
    // London→NY handoff: 13:15-13:45 UTC (795-825 minutes)
    if (total_mins >= 795 && total_mins < 825) {
        return HandoffType::LONDON_TO_NY;
    }
    
    return HandoffType::NONE;
}

void SessionHandoffProfile::onTick(const SHMarketSnapshot& snap) {
    if (!enabled_) {
        idleReason_ = SHIdleReason::GO_NO_GO_BLOCK;
        if (state_ != SHState::IN_TRADE) {
            state_ = SHState::IDLE;
        }
        return;
    }
    
    vwapState_.update(snap.vwapSlope);
    
    if (!hardGatesPass(snap)) {
        if (state_ != SHState::IN_TRADE) {
            if (state_ != SHState::OBSERVING && state_ != SHState::ARMED) {
                state_ = SHState::IDLE;
            }
        }
        return;
    }
    
    HandoffType handoff = detectHandoffWindow(snap);
    
    switch (state_) {
        case SHState::IDLE: {
            if (handoff == HandoffType::NONE) {
                idleReason_ = SHIdleReason::NOT_HANDOFF_WINDOW;
                return;
            }
            
            std::string handoffKey = handoffToString(handoff);
            if (tradedHandoffs_.count(handoffKey) > 0) {
                idleReason_ = SHIdleReason::ALREADY_TRADED_HANDOFF;
                return;
            }
            
            currentHandoff_ = handoff;
            if (handoff == HandoffType::ASIA_TO_LONDON) {
                observingSession_ = "ASIA";
                targetSession_ = "LONDON";
            } else {
                observingSession_ = "LONDON";
                targetSession_ = "NY";
            }
            
            state_ = SHState::OBSERVING;
            stateTs_ns_ = snap.now_ns;
            idleReason_ = SHIdleReason::NO_BIAS_DETERMINED;
            
            printf("[SESSION_HO] Started observing %s for %s handoff\n",
                   observingSession_.c_str(), handoffToString(handoff));
            break;
        }
        
        case SHState::OBSERVING:
            observePriorSession(snap);
            biasAnalysis_ = determineBias(snap.symbol);
            
            if (biasAnalysis_.bias != BiasType::NONE &&
                biasAnalysis_.strength >= getSHConfig(snap.symbol).minBiasStrength) {
                
                state_ = SHState::ARMED;
                stateTs_ns_ = snap.now_ns;
                idleReason_ = SHIdleReason::WAITING_SESSION_OPEN;
                
                printf("[SESSION_HO] Bias determined for %s: %s (%.0f%%) - %s\n",
                       snap.symbol, biasToString(biasAnalysis_.bias),
                       biasAnalysis_.strength * 100.0, biasAnalysis_.reason.c_str());
            }
            break;
            
        case SHState::ARMED:
            evaluateEntry(snap);
            break;
            
        case SHState::IN_TRADE:
            evaluateExit(snap);
            break;
            
        case SHState::DONE:
            idleReason_ = SHIdleReason::ALREADY_TRADED_HANDOFF;
            if (handoff == HandoffType::NONE) {
                state_ = SHState::IDLE;
                currentHandoff_ = HandoffType::NONE;
                biasAnalysis_.reset();
            }
            break;
    }
    
    if (lastSession_ != snap.currentSession) {
        printf("[SESSION_HO] Session changed: %s → %s\n",
               lastSession_.c_str(), snap.currentSession);
        lastSession_ = snap.currentSession;
    }
}

bool SessionHandoffProfile::hardGatesPass(const SHMarketSnapshot& snap) {
    if (!snap.goNoGoIsGo) {
        idleReason_ = SHIdleReason::GO_NO_GO_BLOCK;
        return false;
    }
    
    if (!snap.latencyStable) {
        idleReason_ = SHIdleReason::LATENCY_UNSTABLE;
        return false;
    }
    
    if (snap.shockActive) {
        idleReason_ = SHIdleReason::SHOCK_ACTIVE;
        return false;
    }
    
    if (!isSHSymbolEnabled(snap.symbol)) {
        idleReason_ = SHIdleReason::SYMBOL_DISABLED;
        return false;
    }
    
    if (hasPosition_ && currentSymbol_ != snap.symbol) {
        idleReason_ = SHIdleReason::POSITION_OPEN;
        return true;
    }
    
    return true;
}

void SessionHandoffProfile::observePriorSession(const SHMarketSnapshot& snap) {
    auto& analysis = priorAnalysis_[snap.symbol];
    
    if (analysis.high == 0.0 || snap.mid > analysis.high) {
        analysis.high = snap.mid;
    }
    if (analysis.low == 0.0 || snap.mid < analysis.low) {
        analysis.low = snap.mid;
    }
    
    analysis.vwap = snap.vwap;
    analysis.close = snap.mid;
    analysis.poc = (analysis.high + analysis.low) / 2.0;
    
    double range = analysis.high - analysis.low;
    if (range > 0) {
        double closeRatio = (analysis.close - analysis.low) / range;
        analysis.highRejected = (closeRatio < 0.30);
        analysis.lowRejected = (closeRatio > 0.70);
    }
    
    if (snap.priorSessionVwap > 0) {
        analysis.valueMigration = analysis.vwap - snap.priorSessionVwap;
    }
}

BiasAnalysis SessionHandoffProfile::determineBias(const char* symbol) {
    BiasAnalysis result;
    auto& analysis = priorAnalysis_[symbol];
    
    if (analysis.high == 0.0 || analysis.low == 0.0) {
        return result;
    }
    
    int bullishSignals = 0;
    int bearishSignals = 0;
    std::string reasons;
    
    if (analysis.highRejected) {
        bearishSignals += 2;
        reasons += "HIGH_REJECTED ";
    }
    if (analysis.lowRejected) {
        bullishSignals += 2;
        reasons += "LOW_REJECTED ";
    }
    
    if (analysis.close > analysis.vwap) {
        bullishSignals += 1;
        reasons += "ABOVE_VWAP ";
    } else if (analysis.close < analysis.vwap) {
        bearishSignals += 1;
        reasons += "BELOW_VWAP ";
    }
    
    if (analysis.valueMigration > 0) {
        bullishSignals += 1;
        reasons += "VALUE_UP ";
    } else if (analysis.valueMigration < 0) {
        bearishSignals += 1;
        reasons += "VALUE_DOWN ";
    }
    
    double range = analysis.high - analysis.low;
    if (range > 0) {
        double closeRatio = (analysis.close - analysis.low) / range;
        if (closeRatio > 0.65) {
            bullishSignals += 1;
            reasons += "CLOSE_HIGH ";
        } else if (closeRatio < 0.35) {
            bearishSignals += 1;
            reasons += "CLOSE_LOW ";
        }
    }
    
    int totalSignals = bullishSignals + bearishSignals;
    if (totalSignals == 0) {
        return result;
    }
    
    if (bullishSignals > bearishSignals) {
        result.bias = BiasType::BULLISH;
        result.strength = static_cast<double>(bullishSignals) / (bullishSignals + bearishSignals);
    } else if (bearishSignals > bullishSignals) {
        result.bias = BiasType::BEARISH;
        result.strength = static_cast<double>(bearishSignals) / (bullishSignals + bearishSignals);
    }
    
    result.reason = reasons;
    return result;
}

bool SessionHandoffProfile::confirmBias(const SHMarketSnapshot& snap) {
    auto cfg = getSHConfig(snap.symbol);
    
    if (biasAnalysis_.bias == BiasType::BULLISH) {
        if (snap.mid < snap.vwap) {
            return false;
        }
        if (vwapState_.direction == VwapDirection::DOWN) {
            return false;
        }
    } else if (biasAnalysis_.bias == BiasType::BEARISH) {
        if (snap.mid > snap.vwap) {
            return false;
        }
        if (vwapState_.direction == VwapDirection::UP) {
            return false;
        }
    }
    
    double vwapDist = std::abs(snap.mid - snap.vwap) / snap.mid;
    if (vwapDist < cfg.vwapConfirmPct) {
        return false;
    }
    
    return true;
}

void SessionHandoffProfile::evaluateEntry(const SHMarketSnapshot& snap) {
    if (!confirmBias(snap)) {
        idleReason_ = SHIdleReason::BIAS_NOT_CONFIRMED;
        return;
    }
    
    SHSide side = (biasAnalysis_.bias == BiasType::BULLISH) ? SHSide::BUY : SHSide::SELL;
    enterTrade(side, snap);
}

void SessionHandoffProfile::enterTrade(SHSide side, const SHMarketSnapshot& snap) {
    double riskPct = 0.0020;
    double qty = calculateQty(riskPct, snap);
    
    submitOrder(side, qty, snap.symbol);
    
    hasPosition_ = true;
    positionSide_ = side;
    entryPrice_ = (side == SHSide::BUY) ? snap.ask : snap.bid;
    entryVwap_ = snap.vwap;
    tradeStartNs_ = snap.now_ns;
    currentSymbol_ = snap.symbol;
    tradesThisDay_++;
    
    tradedHandoffs_.insert(handoffToString(currentHandoff_));
    
    state_ = SHState::IN_TRADE;
    idleReason_ = SHIdleReason::NONE;
    
    printf("[SESSION_HO] ENTRY %s %s @ %.5f | Handoff: %s | Bias: %s (%.0f%%)\n",
           shSideToString(side), snap.symbol, entryPrice_,
           handoffToString(currentHandoff_),
           biasToString(biasAnalysis_.bias), biasAnalysis_.strength * 100.0);
}

void SessionHandoffProfile::evaluateExit(const SHMarketSnapshot& snap) {
    if (!hasPosition_) return;
    if (currentSymbol_ != snap.symbol) return;
    
    auto cfg = getSHConfig(currentSymbol_);
    auto& analysis = priorAnalysis_[currentSymbol_];
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    
    if (heldNs > cfg.maxHoldNs) {
        exitTrade("TIME_CAP", snap);
        return;
    }
    
    if (positionSide_ == SHSide::BUY) {
        if (snap.mid < entryVwap_ * 0.998) {
            exitTrade("VWAP_RECLAIM_AGAINST", snap);
            return;
        }
    }
    if (positionSide_ == SHSide::SELL) {
        if (snap.mid > entryVwap_ * 1.002) {
            exitTrade("VWAP_RECLAIM_AGAINST", snap);
            return;
        }
    }
    
    if (analysis.high > 0 && analysis.low > 0) {
        double midpoint = (analysis.high + analysis.low) / 2.0;
        if (positionSide_ == SHSide::BUY && snap.mid < midpoint) {
            exitTrade("STRUCTURE_FAILURE", snap);
            return;
        }
        if (positionSide_ == SHSide::SELL && snap.mid > midpoint) {
            exitTrade("STRUCTURE_FAILURE", snap);
            return;
        }
    }
    
    if (!snap.latencyStable) {
        exitTrade("LATENCY_DEGRADED", snap);
        return;
    }
}

void SessionHandoffProfile::exitTrade(const char* reason, const SHMarketSnapshot& snap) {
    closePosition(reason);
    
    double exitPrice = (positionSide_ == SHSide::BUY) ? snap.bid : snap.ask;
    double pnlBps = 0.0;
    if (entryPrice_ > 0) {
        pnlBps = (exitPrice - entryPrice_) / entryPrice_ * 10000.0;
        if (positionSide_ == SHSide::SELL) pnlBps = -pnlBps;
    }
    
    uint64_t heldNs = snap.now_ns - tradeStartNs_;
    double heldSec = static_cast<double>(heldNs) / 1e9;
    
    printf("[SESSION_HO] EXIT %s: %s @ %.5f | PnL: %.2f bps | Held: %.1f sec\n",
           currentSymbol_.c_str(), reason, exitPrice, pnlBps, heldSec);
    
    hasPosition_ = false;
    positionSide_ = SHSide::NONE;
    entryPrice_ = 0.0;
    entryVwap_ = 0.0;
    
    state_ = SHState::DONE;
    stateTs_ns_ = snap.now_ns;
    idleReason_ = SHIdleReason::ALREADY_TRADED_HANDOFF;
}

void SessionHandoffProfile::onFill(double fillPrice, double qty, bool isBuy) {
    printf("[SESSION_HO] FILL: %s %.6f @ %.5f\n",
           isBuy ? "BUY" : "SELL", qty, fillPrice);
}

void SessionHandoffProfile::onPositionClosed(double pnl_bps, uint64_t heldNs) {
    (void)pnl_bps;
    (void)heldNs;
}

void SessionHandoffProfile::onTimer(uint64_t now_ns) {
    (void)now_ns;
}

void SessionHandoffProfile::printStatus() const {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  SESSION HANDOFF STATUS                                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  State:     %-15s                                     ║\n", shStateToString(state_));
    printf("║  Idle:      %s %-20s                          ║\n",
           shIdleReasonIcon(idleReason_), shIdleReasonToString(idleReason_));
    printf("║  Handoff:   %-15s                                     ║\n", handoffToString(currentHandoff_));
    printf("║  Bias:      %-10s (%.0f%%)                               ║\n",
           biasToString(biasAnalysis_.bias), biasAnalysis_.strength * 100.0);
    printf("║  Position:  %s                                              ║\n",
           hasPosition_ ? "YES" : "NO");
    printf("║  Trades:    %d/day                                           ║\n", tradesThisDay_);
    printf("║  Enabled:   %s                                              ║\n",
           enabled_ ? "YES" : "NO");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

void SessionHandoffProfile::toJSON(char* buf, size_t buf_size) const {
    snprintf(buf, buf_size,
        "{"
        "\"profile\":\"SESSION_HANDOFF\","
        "\"state\":\"%s\","
        "\"idle_reason\":\"%s\","
        "\"handoff\":\"%s\","
        "\"bias\":\"%s\","
        "\"bias_strength\":%.2f,"
        "\"trades_day\":%d,"
        "\"has_position\":%s,"
        "\"enabled\":%s"
        "}",
        shStateToString(state_),
        shIdleReasonToString(idleReason_),
        handoffToString(currentHandoff_),
        biasToString(biasAnalysis_.bias),
        biasAnalysis_.strength,
        tradesThisDay_,
        hasPosition_ ? "true" : "false",
        enabled_.load() ? "true" : "false"
    );
}

} // namespace Chimera
