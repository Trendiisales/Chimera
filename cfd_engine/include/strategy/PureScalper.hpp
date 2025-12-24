// =============================================================================
// PureScalper.hpp v7.09 - BALANCED - Trades But With Discipline
// =============================================================================
// v7.09 BALANCED:
//   - Confidence stays at 0.60 (was working before)
//   - shouldTrade() threshold at 0.50 (allow trades)
//   - TP/SL: 30/15 bps (original working values)
//   - Max spread: 15 bps (reasonable for metals)
//   - Mean reversion kept but with stricter threshold
//   - RELIES ON MicroStateMachine for cooldown enforcement
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <string>
#include "MicroStateMachine.hpp"

namespace Omega {

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

    // v7.09: Back to 0.50 threshold - let MicroStateMachine do the gating
    bool shouldTrade() const { return direction != 0 && confidence >= 0.50; }
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

    void init(double b, double a) {
        bid = b; ask = a; mid = (b + a) / 2; spread = a - b;
        ema_fast = ema_slow = vwap = mid;
        price_sum = mid; price_count = 1;
        micro_vol = 0.0001; ticks = 1;
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
        
        // v7.09: Back to original working TP/SL
        double tp_bps = 30.0;
        double sl_bps = 15.0;
        double trail_start = 10.0;
        double trail_stop = 5.0;
        
        double max_spread = 15.0;    // v7.09: Reasonable for metals
        int max_hold = 300;
        int warmup = 30;
        
        bool debug = true;
        double contract_size = 100.0;
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

        // === EXIT LOGIC FIRST ===
        if (st.pos.active) {
            sig = checkExit(st, ts, micro);
            if (sig.direction != 0) {
                // CRITICAL: Notify micro state machine of exit -> starts cooldown
                micro.onExit(ts);
                if (cfg_.debug) {
                    std::cout << "[SCALP] EXIT " << symbol << " reason=" << sig.reason
                              << " pnl_bps=" << sig.realized_pnl_bps << "\n";
                }
            }
            return sig;
        }

        // === ENTRY LOGIC ===
        double sprdBps = st.spreadBps();

        // Spread check
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

            // Only mean-revert if deviation > 0.5% AND low volatility
            if (std::fabs(dev) > 0.005 && vol_z < 5.0) {
                dir = (dev > 0) ? -1 : 1;
            }
        }

        if (dir == 0) {
            sig.reason = "NO_SIGNAL";
            return sig;
        }

        // === MICRO STATE GATE - This is where cooldown is enforced ===
        MicroDecision decision = micro.allowEntry(dir, sprdBps, cfg_.tp_bps);
        sig.micro_state = decision.current_state;
        sig.veto_reason = decision.veto;

        if (!decision.allow_trade) {
            sig.reason = vetoStr(decision.veto);
            return sig;
        }

        // === EXECUTE ENTRY ===
        sig.direction = dir;
        sig.confidence = 0.60;
        sig.size = cfg_.size;
        sig.reason = (dir > 0) ? "BUY" : "SELL";

        st.pos.open(dir, st.mid, cfg_.size, ts);

        // CRITICAL: Notify micro state machine of entry
        micro.onEntry(dir, ts);

        if (cfg_.debug) {
            std::cout << "\n*** [SCALP] ENTRY " << symbol << " " << sig.reason
                      << " @" << st.mid << " spread=" << sprdBps << "bps ***\n\n";
        }

        return sig;
    }

    ScalpSignal checkExit(SymbolState& st, uint64_t ts, MicroStateMachine& micro) {
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

        // Time exit (requires min hold)
        if (canExit && pos.ticks_held >= cfg_.max_hold) {
            sig.direction = -pos.side;
            sig.size = pos.size;
            sig.confidence = 1.0;
            sig.reason = "TIME";
            sig.is_exit = true;
            sig.realized_pnl_bps = pnl_bps;
            sig.realized_pnl = calcCurrencyPnL();
            sig.entry_price = pos.entry_price;
            sig.exit_price = st.mid;
            pos.close();
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
};

} // namespace Omega
