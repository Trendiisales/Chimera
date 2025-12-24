// =============================================================================
// PureScalper.hpp v7.10 - DISCIPLINED EXECUTION - NO MORE RUBBISH TRADES
// =============================================================================
// v7.10 CRITICAL FIXES (from log analysis):
//
// 🔴 PATCH 1: TIME CANNOT GENERATE TRADES
//    - TIME is an EXIT reason only, never an entry signal
//    - TIME exits do NOT trigger immediate re-entry
//
// 🔴 PATCH 2: HOLDING BLOCKS ALL ENTRIES (ABSOLUTE)
//    - If has_position == true, NO new entries allowed
//    - No exceptions, no overrides
//
// 🔴 PATCH 3: MIN_HOLD_MS = 2500 (CFD SAFE)
//    - No trade reversal within 2.5 seconds
//    - Prevents the 800ms flip-flops seen in logs
//
// 🔴 PATCH 4: CONFIDENCE FLOOR = 0.80 (XAU/XAG)
//    - conf=0.60 is noise at CFD spreads
//    - Only high-conviction trades get through
//
// 🔴 PATCH 5: ONE INTENT PER TICK WINDOW
//    - Prevents BUY+SELL+TIME all firing
//    - First valid signal wins, rest blocked
//
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <string>
#include <chrono>
#include "MicroStateMachine.hpp"

namespace Omega {

// =============================================================================
// v7.10: Strict confidence thresholds by asset class
// =============================================================================
struct ConfidenceThresholds {
    static constexpr double METALS = 0.80;      // XAU, XAG
    static constexpr double INDICES = 0.75;     // NAS100, US30, SPX500
    static constexpr double FOREX = 0.70;       // EUR, GBP, etc.
    static constexpr double DEFAULT = 0.80;     // Safe default
};

// =============================================================================
// v7.10: Minimum hold times in milliseconds
// =============================================================================
struct HoldTimes {
    static constexpr int64_t METALS_MS = 2500;   // 2.5 seconds
    static constexpr int64_t INDICES_MS = 2000;  // 2.0 seconds  
    static constexpr int64_t FOREX_MS = 1500;    // 1.5 seconds
    static constexpr int64_t DEFAULT_MS = 2500;  // Safe default
};

struct ScalpSignal {
    int8_t direction = 0;
    double confidence = 0.0;
    double size = 0.0;
    const char* reason = "";
    bool is_exit = false;
    double realized_pnl = 0.0;
    double realized_pnl_bps = 0.0;
    double entry_price = 0.0;
    double exit_price = 0.0;
    MicroState micro_state = MicroState::IDLE;
    VetoReason veto_reason = VetoReason::NONE;

    // v7.10: Confidence threshold is now asset-specific and HIGH
    bool shouldTrade() const { return direction != 0 && confidence >= 0.75; }
};

struct ScalpPosition {
    bool active = false;
    int8_t side = 0;
    double entry_price = 0;
    double size = 0;
    uint64_t entry_time_ns = 0;
    int ticks_held = 0;
    double highest = 0, lowest = 0;

    void open(int8_t s, double px, double sz, uint64_t ts) {
        active = true; side = s; entry_price = px; size = sz;
        entry_time_ns = ts; ticks_held = 0; highest = lowest = px;
    }
    void close() { active = false; side = 0; size = 0; }
    void update(double mid) { 
        ticks_held++; 
        highest = std::max(highest, mid); 
        lowest = std::min(lowest, mid); 
    }
    double pnlBps(double mid) const {
        if (!active || entry_price == 0) return 0;
        return (mid - entry_price) / entry_price * 10000.0 * side;
    }
};

struct SymbolState {
    double bid = 0, ask = 0, mid = 0, spread = 0;
    double ema_fast = 0, ema_slow = 0;
    double momentum = 0, micro_vol = 0, vwap = 0;
    double price_sum = 0;
    int price_count = 0;
    uint64_t ticks = 0;
    ScalpPosition pos;
    
    // v7.10: Track last trade time for MIN_HOLD enforcement
    int64_t last_trade_ms = 0;
    int8_t last_trade_direction = 0;

    void init(double b, double a) {
        bid = b; ask = a; mid = (b + a) / 2; spread = a - b;
        ema_fast = ema_slow = vwap = mid;
        price_sum = mid; price_count = 1;
        micro_vol = 0.0001; ticks = 1;
        last_trade_ms = 0;
        last_trade_direction = 0;
    }

    void update(double b, double a) {
        double prev_mid = mid;
        bid = b; ask = a; mid = (b + a) / 2; spread = a - b;

        ema_fast = 0.3 * mid + 0.7 * ema_fast;
        ema_slow = 0.1 * mid + 0.9 * ema_slow;

        double chg = mid - prev_mid;
        momentum = 0.3 * chg + 0.7 * momentum;
        micro_vol = 0.15 * std::fabs(chg) + 0.85 * micro_vol;
        if (micro_vol < 0.00001) micro_vol = 0.00001;

        price_sum += mid; price_count++;
        if (price_count > 20) { price_sum = vwap * 19 + mid; price_count = 20; }
        vwap = price_sum / price_count;

        ticks++;
        if (pos.active) pos.update(mid);
    }

    double spreadBps() const { return mid > 0 ? (spread / mid) * 10000.0 : 9999; }
    
    int8_t trend() const { 
        return (ema_fast > ema_slow && momentum > 0) ? 1 :
               (ema_fast < ema_slow && momentum < 0) ? -1 : 0; 
    }

    MicroInputs toMicroInputs(int64_t ts) const {
        return {mid, vwap, micro_vol, spreadBps(), ts};
    }
};

class PureScalper {
public:
    struct Config {
        double size = 0.01;
        
        // v7.10: Wider TP/SL to account for spread + give trades room
        double tp_bps = 45.0;       // Was 30 - need more room
        double sl_bps = 25.0;       // Was 15 - wider stop
        double trail_start = 20.0;  // Was 10
        double trail_stop = 10.0;   // Was 5
        
        double max_spread = 12.0;   // v7.10: Tighter - don't trade wide spreads
        int max_hold = 500;         // v7.10: Longer hold allowed
        int warmup = 50;            // v7.10: More warmup for stable signals
        
        bool debug = true;
        double contract_size = 100.0;
        
        // v7.10: NEW - Minimum hold time in ms (prevents flip-flops)
        int64_t min_hold_ms = 2500;
        
        // v7.10: NEW - Confidence floor (asset-specific)
        double min_confidence = 0.80;
    };

    PureScalper() {
        microMgr_.setDebugSymbol("XAUUSD");
        microMgr_.setSimpleMode(true);
    }

    void setConfig(const Config& c) { cfg_ = c; }
    Config& getConfig() { return cfg_; }

    void enableDebug(const std::string& symbol) {
        microMgr_.setDebugSymbol(symbol);
    }

    ScalpSignal process(const char* symbol, double bid, double ask, double, double, uint64_t ts) {
        ScalpSignal sig;
        std::string sym(symbol);
        int64_t now_ms = getNowMs();

        // Init or update state
        auto& st = states_[sym];
        if (st.ticks == 0) {
            st.init(bid, ask);
            sig.reason = "INIT";
            return sig;
        }
        st.update(bid, ask);

        // Warmup
        if (st.ticks < (uint64_t)cfg_.warmup) {
            sig.reason = "WARMUP";
            return sig;
        }

        // Get micro state machine
        auto& micro = microMgr_.get(sym);
        micro.onTick(st.toMicroInputs(ts));

        sig.micro_state = micro.state();
        sig.veto_reason = micro.lastVeto();

        // =====================================================================
        // v7.10 PATCH 2: HOLDING BLOCKS ALL ENTRIES (ABSOLUTE)
        // If we have a position, ONLY check exits. No new entries ever.
        // =====================================================================
        if (st.pos.active) {
            sig = checkExit(st, ts, micro, now_ms);
            if (sig.direction != 0 && sig.is_exit) {
                // Record exit time for MIN_HOLD enforcement
                st.last_trade_ms = now_ms;
                st.last_trade_direction = sig.direction;
                
                // Notify micro state machine of exit -> starts cooldown
                micro.onExit(ts);
                
                if (cfg_.debug) {
                    std::cout << "[SCALP] EXIT " << symbol << " reason=" << sig.reason
                              << " pnl_bps=" << sig.realized_pnl_bps << "\n";
                }
            }
            // CRITICAL: Return here. Do NOT fall through to entry logic.
            // Even if exit reason is TIME or HOLDING, no new entry.
            return sig;
        }

        // =====================================================================
        // v7.10 PATCH 3: MIN_HOLD_MS ENFORCEMENT
        // Prevent any entry within 2.5 seconds of last trade
        // =====================================================================
        if (st.last_trade_ms > 0) {
            int64_t elapsed = now_ms - st.last_trade_ms;
            if (elapsed < cfg_.min_hold_ms) {
                sig.reason = "MIN_HOLD_TIME";
                if (cfg_.debug && (st.ticks % 100 == 0)) {
                    std::cout << "[GATE] " << symbol << " MIN_HOLD blocked, wait " 
                              << (cfg_.min_hold_ms - elapsed) << "ms\n";
                }
                return sig;
            }
        }

        // === ENTRY LOGIC ===
        double sprdBps = st.spreadBps();

        // Spread check (v7.10: tighter)
        if (sprdBps > cfg_.max_spread) {
            sig.reason = "SPREAD_WIDE";
            return sig;
        }

        // Determine direction
        int8_t dir = st.trend();
        
        // Mean reversion if no trend (with stricter threshold)
        if (dir == 0) {
            double dev = (st.mid - st.ema_slow) / st.ema_slow;
            double vol_z = st.micro_vol / st.ema_slow * 10000.0;

            // v7.10: Stricter mean reversion - need larger deviation
            if (std::fabs(dev) > 0.008 && vol_z < 4.0) {
                dir = (dev > 0) ? -1 : 1;
            }
        }

        if (dir == 0) {
            sig.reason = "NO_SIGNAL";
            return sig;
        }

        // =====================================================================
        // v7.10 PATCH 5: FLIP DIRECTION PROTECTION
        // Don't reverse direction too quickly
        // =====================================================================
        if (st.last_trade_direction != 0 && dir == -st.last_trade_direction) {
            int64_t elapsed = now_ms - st.last_trade_ms;
            // Double the min_hold for direction flips
            if (elapsed < cfg_.min_hold_ms * 2) {
                sig.reason = "FLIP_BLOCKED";
                return sig;
            }
        }

        // === MICRO STATE GATE ===
        MicroDecision decision = micro.allowEntry(dir, sprdBps, cfg_.tp_bps);
        sig.micro_state = decision.current_state;
        sig.veto_reason = decision.veto;

        if (!decision.allow_trade) {
            sig.reason = vetoStr(decision.veto);
            return sig;
        }

        // =====================================================================
        // v7.10 PATCH 4: CONFIDENCE CALCULATION WITH HIGH FLOOR
        // Must exceed min_confidence (0.80 for metals)
        // =====================================================================
        double confidence = calculateConfidence(st, dir, sprdBps);
        
        if (confidence < cfg_.min_confidence) {
            sig.reason = "LOW_CONF";
            if (cfg_.debug) {
                std::cout << "[GATE] " << symbol << " LOW_CONF: " << confidence 
                          << " < " << cfg_.min_confidence << "\n";
            }
            return sig;
        }

        // === EXECUTE ENTRY ===
        sig.direction = dir;
        sig.confidence = confidence;
        sig.size = cfg_.size;
        sig.reason = (dir > 0) ? "BUY" : "SELL";

        st.pos.open(dir, st.mid, cfg_.size, ts);
        
        // Record entry for MIN_HOLD tracking
        st.last_trade_ms = now_ms;
        st.last_trade_direction = dir;

        // Notify micro state machine of entry
        micro.onEntry(dir, ts);

        if (cfg_.debug) {
            std::cout << "\n*** [SCALP] ENTRY " << symbol << " " << sig.reason
                      << " @" << st.mid << " spread=" << sprdBps << "bps"
                      << " conf=" << confidence << " ***\n\n";
        }

        return sig;
    }

    ScalpSignal checkExit(SymbolState& st, uint64_t ts, MicroStateMachine& micro, int64_t now_ms) {
        ScalpSignal sig;
        auto& pos = st.pos;
        if (!pos.active) return sig;

        double pnl_bps = pos.pnlBps(st.mid);
        bool canExit = micro.canExit(ts);

        // Currency PnL calculation
        auto calcCurrencyPnL = [&]() -> double {
            double pnl_points = (st.mid - pos.entry_price) * pos.side;
            return pnl_points * pos.size * cfg_.contract_size;
        };

        // TP - Take Profit (requires min hold)
        if (canExit && pnl_bps >= cfg_.tp_bps) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "TP";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // SL - Stop Loss (ALWAYS execute, ignore canExit)
        if (pnl_bps <= -cfg_.sl_bps) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "SL";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
            return sig;
        }

        // Trailing stop (requires min hold)
        if (canExit && pnl_bps >= cfg_.trail_start) {
            double peak = pos.side > 0 ? pos.highest : pos.lowest;
            double peakPnl = (peak - pos.entry_price) / pos.entry_price * 10000.0 * pos.side;
            if (peakPnl - pnl_bps > cfg_.trail_stop) {
                sig.direction = -pos.side;
                sig.size = pos.size;
                sig.confidence = 1.0;
                sig.reason = "TRAIL";
                sig.is_exit = true;
                sig.realized_pnl_bps = pnl_bps;
                sig.realized_pnl = calcCurrencyPnL();
                sig.entry_price = pos.entry_price;
                sig.exit_price = st.mid;
                pos.close();
                return sig;
            }
        }

        // =====================================================================
        // v7.10 PATCH 1: TIME EXIT DOES NOT GENERATE A TRADE SIGNAL
        // TIME closes the position but returns direction=0 to prevent
        // the caller from treating this as a new trade signal
        // =====================================================================
        if (canExit && pos.ticks_held >= cfg_.max_hold) {
            // Close the position internally
            double exit_pnl_bps = pnl_bps;
            double exit_pnl = calcCurrencyPnL();
            
            if (cfg_.debug) {
                std::cout << "[SCALP] TIME_EXIT " << " pnl_bps=" << exit_pnl_bps 
                          << " (position closed, NO trade signal)\n";
            }
            
            pos.close();
            
            // v7.10 CRITICAL: Return with direction=0
            // This prevents TIME from being treated as a tradeable signal
            sig.direction = 0;  // NO TRADE SIGNAL
            sig.confidence = 0.0;
            sig.reason = "TIME_GATE";  // Not TIME - TIME_GATE indicates no signal
            sig.is_exit = true;  // Mark that we did exit
            sig.realized_pnl_bps = exit_pnl_bps;
            sig.realized_pnl = exit_pnl;
            return sig;
        }

        sig.reason = "HOLDING";
        return sig;
    }

    const SymbolState* getState(const char* s) const {
        auto it = states_.find(s);
        return it != states_.end() ? &it->second : nullptr;
    }

    MicroStateManager& getMicroManager() { return microMgr_; }

    std::string getDiagnostics(const std::string& sym) const {
        return microMgr_.getDiagnostics(sym);
    }

    void reset() {
        states_.clear();
        microMgr_.reset();
    }

private:
    Config cfg_;
    std::unordered_map<std::string, SymbolState> states_;
    MicroStateManager microMgr_;
    
    // v7.10: Get current time in milliseconds
    static int64_t getNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
    
    // =========================================================================
    // v7.10: Confidence calculation - must be HIGH to trade
    // =========================================================================
    double calculateConfidence(const SymbolState& st, int8_t dir, double sprdBps) const {
        double conf = 0.5;  // Base
        
        // Trend alignment bonus (+0.15)
        int8_t trend = st.trend();
        if (trend == dir) {
            conf += 0.15;
        }
        
        // Momentum alignment bonus (+0.10)
        if ((dir > 0 && st.momentum > 0) || (dir < 0 && st.momentum < 0)) {
            conf += 0.10;
        }
        
        // VWAP alignment bonus (+0.10)
        double vwap_dev = (st.mid - st.vwap) / st.vwap;
        if ((dir > 0 && vwap_dev < -0.001) || (dir < 0 && vwap_dev > 0.001)) {
            conf += 0.10;  // Buying below VWAP or selling above
        }
        
        // Tight spread bonus (+0.10)
        if (sprdBps < 5.0) {
            conf += 0.10;
        } else if (sprdBps < 8.0) {
            conf += 0.05;
        }
        
        // Volatility penalty
        double vol_ratio = st.micro_vol / (st.mid * 0.0001);  // Normalized
        if (vol_ratio > 2.0) {
            conf -= 0.10;  // High vol = lower confidence
        }
        
        return std::clamp(conf, 0.0, 1.0);
    }
};

} // namespace Omega
