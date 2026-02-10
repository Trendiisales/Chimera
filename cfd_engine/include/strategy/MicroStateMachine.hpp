// =============================================================================
// MicroStateMachine.hpp - v7.09 BALANCED - Cooldown Works, Trading Works
// =============================================================================
// v7.09 BALANCED FIX:
//   - Cooldown enforced in simple_mode (was the bug)
//   - But parameters are REASONABLE for actual trading:
//     * cooldown_ms = 1000 (1 second, not 3)
//     * min_hold_ms = 500 (0.5 second, not 2.5)
//     * flip_cooldown_ms = 2000 (2 seconds between direction changes)
//   - Confidence check REMOVED from MicroStateMachine (belongs in PureScalper)
//   - Spread check uses symbol-appropriate values
// =============================================================================
#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iostream>

namespace Omega {

enum class MicroState { IDLE, IMPULSE, IN_POSITION, COOLDOWN, LOCKED };
enum class VetoReason { NONE, NO_IMPULSE, NO_EXHAUSTION, COOLDOWN_ACTIVE, DIRECTION_LOCK,
                        CHURN_LOCK, SPREAD_WIDE, NO_EDGE, MICRO_VOL_ZERO, LOCK_EXPIRED, 
                        WARMUP, MIN_HOLD, FLIP_BLOCKED };
enum class MicroProfile { CONSERVATIVE, BALANCED, AGGRESSIVE };

inline const char* stateStr(MicroState s) {
    switch(s) { 
        case MicroState::IDLE: return "IDLE"; 
        case MicroState::IMPULSE: return "IMPULSE";
        case MicroState::IN_POSITION: return "IN_POS"; 
        case MicroState::COOLDOWN: return "COOL";
        case MicroState::LOCKED: return "LOCK"; 
        default: return "?"; 
    }
}

inline const char* vetoStr(VetoReason r) {
    switch(r) { 
        case VetoReason::NONE: return "NONE"; 
        case VetoReason::NO_IMPULSE: return "NO_IMP";
        case VetoReason::NO_EXHAUSTION: return "NO_EXH"; 
        case VetoReason::COOLDOWN_ACTIVE: return "COOL";
        case VetoReason::DIRECTION_LOCK: return "DIR_LOCK"; 
        case VetoReason::CHURN_LOCK: return "CHURN";
        case VetoReason::SPREAD_WIDE: return "SPREAD"; 
        case VetoReason::NO_EDGE: return "NO_EDGE";
        case VetoReason::WARMUP: return "WARMUP"; 
        case VetoReason::MIN_HOLD: return "MIN_HOLD";
        case VetoReason::FLIP_BLOCKED: return "FLIP_BLOCK";
        default: return "?"; 
    }
}

inline const char* profStr(MicroProfile p) {
    switch(p) { 
        case MicroProfile::CONSERVATIVE: return "CONS"; 
        case MicroProfile::BALANCED: return "BAL";
        case MicroProfile::AGGRESSIVE: return "AGG"; 
        default: return "?"; 
    }
}

struct MicroInputs {
    double last_price = 0, vwap = 0, micro_vol = 0, spread_bps = 0;
    int64_t now_ns = 0;
};

struct MicroDecision {
    bool allow_trade = false;
    VetoReason veto = VetoReason::NONE;
    int impulse_dir = 0;
    MicroState current_state = MicroState::IDLE;
};

struct MicroProfileParams {
    double impulse_mult = 0.8;
    double exhaustion_mult = 0.5;
    int exhaustion_ticks = 2;
    
    // v7.09: BALANCED parameters - strict enough to prevent flip-flops, loose enough to trade
    int cooldown_ms = 1000;          // 1 second cooldown after exit
    int min_hold_ms = 500;           // 0.5 second minimum hold
    int flip_cooldown_ms = 2000;     // 2 seconds before reversing direction
    
    int churn_flip_limit = 4;        // 4 flips in window triggers lock
    int churn_window_ms = 60000;     // 60 second window
    int lock_duration_ms = 30000;    // 30 second lock when churn detected
    
    int warmup_ticks = 10;  // v4.2.2: Reduced from 30 (CFD markets sparse)
    double max_spread_bps = 20.0;    // Let PureScalper handle tighter spread checks
    double min_edge_bps = 0.0;       // Edge check done in PureScalper
    
    bool simple_mode = true;

    static MicroProfileParams balanced() {
        MicroProfileParams p;
        p.cooldown_ms = 1000;
        p.min_hold_ms = 500;
        p.flip_cooldown_ms = 2000;
        p.churn_flip_limit = 4;
        p.churn_window_ms = 60000;
        p.lock_duration_ms = 30000;
        p.warmup_ticks = 10;  // v4.2.2: Reduced from 30
        p.max_spread_bps = 20.0;
        p.min_edge_bps = 0.0;
        p.simple_mode = true;
        return p;
    }
};

class MicroStateMachine {
public:
    static constexpr int64_t NS_PER_MS = 1000000LL;

    MicroStateMachine() : params_(MicroProfileParams::balanced()) {}

    void setParams(const MicroProfileParams& p) { params_ = p; }
    void setSimpleMode(bool enabled) { params_.simple_mode = enabled; }
    void setDebug(bool d) { debug_ = d; }

    void onTick(const MicroInputs& in) {
        total_ticks_++;
        last_input_ = in;
        current_time_ns_ = in.now_ns;

        // Warmup
        if (total_ticks_ < params_.warmup_ticks) {
            last_veto_ = VetoReason::WARMUP;
            return;
        }

        // =====================================================================
        // v7.09: ALWAYS process state transitions (was broken in simple_mode)
        // =====================================================================
        
        // Lock expiry
        if (state_ == MicroState::LOCKED && in.now_ns >= lock_until_ns_) {
            state_ = MicroState::IDLE;
            direction_flips_ = 0;
            if (debug_) std::cout << "[MICRO] LOCK_EXPIRED -> IDLE\n";
        }

        // Cooldown expiry
        if (state_ == MicroState::COOLDOWN) {
            int64_t elapsed = (in.now_ns - cooldown_start_ns_) / NS_PER_MS;
            if (elapsed >= params_.cooldown_ms) {
                state_ = MicroState::IDLE;
                if (debug_) {
                    std::cout << "[MICRO] COOLDOWN_DONE -> IDLE (was " << elapsed << "ms)\n";
                }
            }
        }

        // Decay churn counter
        if (last_flip_time_ns_ > 0 && 
            (in.now_ns - last_flip_time_ns_) / NS_PER_MS > params_.churn_window_ms) {
            if (direction_flips_ > 0) {
                direction_flips_--;
                last_flip_time_ns_ = in.now_ns;
            }
        }

        // Simple mode: skip impulse detection
        if (params_.simple_mode) {
            return;
        }

        // Full impulse detection (only if not simple_mode)
        if (state_ == MicroState::IDLE) {
            double disp = std::fabs(in.last_price - in.vwap);
            double thresh = params_.impulse_mult * in.micro_vol;

            if (in.micro_vol > 0 && disp >= thresh) {
                state_ = MicroState::IMPULSE;
                state_ts_ns_ = in.now_ns;
                impulse_dir_ = (in.last_price > in.vwap) ? -1 : +1;
                exhaustion_ticks_ = 0;
                if (debug_) std::cout << "[MICRO] IMPULSE dir=" << impulse_dir_ << "\n";
            }
        }

        if (state_ == MicroState::IMPULSE) {
            bool stalled = (prev_price_ > 0) &&
                          (std::fabs(in.last_price - prev_price_) <= params_.exhaustion_mult * in.micro_vol);
            if (stalled) exhaustion_ticks_++;
            else exhaustion_ticks_ = 0;
        }

        prev_price_ = in.last_price;
    }

    MicroDecision allowEntry(int direction, double spread_bps, double /*tp_bps*/) {
        MicroDecision d;
        d.current_state = state_;
        d.impulse_dir = impulse_dir_;

        // Debug
        if (debug_) {
            std::cout << "[ALLOW] state=" << stateStr(state_)
                      << " dir=" << direction
                      << " last_dir=" << last_trade_direction_
                      << " spread=" << spread_bps << "\n";
        }

        // =====================================================================
        // CORE CHECKS - Apply to ALL modes
        // =====================================================================

        // 1. Warmup
        if (total_ticks_ < params_.warmup_ticks) {
            d.veto = VetoReason::WARMUP;
            last_veto_ = d.veto;
            return d;
        }

        // 2. Locked (churn detected)
        if (state_ == MicroState::LOCKED) {
            d.veto = VetoReason::CHURN_LOCK;
            last_veto_ = d.veto;
            return d;
        }

        // 3. v7.09 FIX: Cooldown check - THIS WAS MISSING IN SIMPLE MODE
        if (state_ == MicroState::COOLDOWN) {
            d.veto = VetoReason::COOLDOWN_ACTIVE;
            last_veto_ = d.veto;
            if (debug_) {
                int64_t remaining = params_.cooldown_ms - (current_time_ns_ - cooldown_start_ns_) / NS_PER_MS;
                std::cout << "[ALLOW] BLOCKED: COOLDOWN remaining=" << remaining << "ms\n";
            }
            return d;
        }

        // 4. Already in position
        if (state_ == MicroState::IN_POSITION) {
            d.veto = VetoReason::COOLDOWN_ACTIVE;
            last_veto_ = d.veto;
            return d;
        }

        // 5. Flip prevention - can't reverse direction too quickly
        if (last_trade_direction_ != 0 && direction != 0 && direction != last_trade_direction_) {
            int64_t since_last = (current_time_ns_ - last_trade_time_ns_) / NS_PER_MS;
            if (since_last < params_.flip_cooldown_ms) {
                d.veto = VetoReason::FLIP_BLOCKED;
                last_veto_ = d.veto;
                if (debug_) {
                    std::cout << "[ALLOW] BLOCKED: FLIP too soon (" << since_last 
                              << "/" << params_.flip_cooldown_ms << "ms)\n";
                }
                return d;
            }
        }

        // 6. Spread check (loose - PureScalper has tighter check)
        if (spread_bps > params_.max_spread_bps) {
            d.veto = VetoReason::SPREAD_WIDE;
            last_veto_ = d.veto;
            return d;
        }

        // 7. Churn check
        if (direction_flips_ >= params_.churn_flip_limit) {
            state_ = MicroState::LOCKED;
            lock_until_ns_ = current_time_ns_ + params_.lock_duration_ms * NS_PER_MS;
            d.veto = VetoReason::CHURN_LOCK;
            last_veto_ = d.veto;
            if (debug_) {
                std::cout << "[ALLOW] CHURN_LOCK triggered, flips=" << direction_flips_ << "\n";
            }
            return d;
        }

        // =====================================================================
        // Simple mode: passed all checks, allow trade
        // =====================================================================
        if (params_.simple_mode) {
            d.allow_trade = true;
            d.veto = VetoReason::NONE;
            last_veto_ = VetoReason::NONE;
            if (debug_) std::cout << "[ALLOW] TRADE ALLOWED\n";
            return d;
        }

        // =====================================================================
        // Full mode: additional impulse checks
        // =====================================================================
        if (state_ != MicroState::IMPULSE) {
            d.veto = VetoReason::NO_IMPULSE;
            last_veto_ = d.veto;
            return d;
        }

        if (direction != impulse_dir_) {
            d.veto = VetoReason::DIRECTION_LOCK;
            last_veto_ = d.veto;
            return d;
        }

        if (exhaustion_ticks_ < params_.exhaustion_ticks) {
            d.veto = VetoReason::NO_EXHAUSTION;
            last_veto_ = d.veto;
            return d;
        }

        d.allow_trade = true;
        d.veto = VetoReason::NONE;
        last_veto_ = VetoReason::NONE;
        return d;
    }

    void onEntry(int direction, int64_t now_ns) {
        if (debug_) {
            std::cout << "[MICRO] ON_ENTRY dir=" << direction 
                      << " prev=" << last_trade_direction_ << "\n";
        }

        // Track direction flips
        if (last_trade_direction_ != 0 && direction != last_trade_direction_) {
            direction_flips_++;
            last_flip_time_ns_ = now_ns;
            if (debug_) {
                std::cout << "[MICRO] FLIP detected, count=" << direction_flips_ << "\n";
            }
        }
        
        last_trade_direction_ = direction;
        last_trade_time_ns_ = now_ns;
        entry_time_ns_ = now_ns;

        state_ = MicroState::IN_POSITION;
        state_ts_ns_ = now_ns;

        // Check for churn lock
        if (direction_flips_ >= params_.churn_flip_limit) {
            state_ = MicroState::LOCKED;
            lock_until_ns_ = now_ns + params_.lock_duration_ms * NS_PER_MS;
            if (debug_) {
                std::cout << "[MICRO] CHURN_LOCK on entry, flips=" << direction_flips_ << "\n";
            }
        }
    }

    void onExit(int64_t now_ns) {
        if (debug_) {
            int64_t held = (now_ns - entry_time_ns_) / NS_PER_MS;
            std::cout << "[MICRO] ON_EXIT held=" << held << "ms -> COOLDOWN\n";
        }
        
        state_ = MicroState::COOLDOWN;
        cooldown_start_ns_ = now_ns;
        state_ts_ns_ = now_ns;
    }

    bool canExit(int64_t now_ns) const {
        if (state_ != MicroState::IN_POSITION) return true;
        int64_t held_ms = (now_ns - entry_time_ns_) / NS_PER_MS;
        return held_ms >= params_.min_hold_ms;
    }

    // Getters
    MicroState state() const { return state_; }
    VetoReason lastVeto() const { return last_veto_; }
    int64_t totalTicks() const { return total_ticks_; }
    int impulseDirection() const { return impulse_dir_; }
    const MicroProfileParams& params() const { return params_; }
    int lastTradeDirection() const { return last_trade_direction_; }
    int flipCount() const { return direction_flips_; }
    
    int64_t cooldownRemainingMs() const {
        if (state_ != MicroState::COOLDOWN) return 0;
        int64_t elapsed = (current_time_ns_ - cooldown_start_ns_) / NS_PER_MS;
        int64_t remaining = params_.cooldown_ms - elapsed;
        return remaining > 0 ? remaining : 0;
    }

    void reset() {
        state_ = MicroState::IDLE;
        total_ticks_ = 0;
        direction_flips_ = 0;
        last_trade_direction_ = 0;
        last_trade_time_ns_ = 0;
        cooldown_start_ns_ = 0;
    }

private:
    MicroProfileParams params_;
    MicroState state_ = MicroState::IDLE;
    VetoReason last_veto_ = VetoReason::NONE;

    int64_t total_ticks_ = 0;
    int64_t state_ts_ns_ = 0;
    int64_t lock_until_ns_ = 0;
    int64_t current_time_ns_ = 0;
    int64_t cooldown_start_ns_ = 0;
    int64_t entry_time_ns_ = 0;
    int64_t last_trade_time_ns_ = 0;
    int64_t last_flip_time_ns_ = 0;

    int impulse_dir_ = 0;
    int exhaustion_ticks_ = 0;
    double prev_price_ = 0;

    int direction_flips_ = 0;
    int last_trade_direction_ = 0;

    MicroInputs last_input_;
    bool debug_ = false;
};

// =============================================================================
// MULTI-SYMBOL MANAGER
// =============================================================================
class MicroStateManager {
public:
    MicroStateManager() {
        default_params_ = MicroProfileParams::balanced();
    }

    void setDebugSymbol(const std::string& sym) {
        debug_symbol_ = sym;
        auto it = machines_.find(sym);
        if (it != machines_.end()) {
            it->second.setDebug(true);
        }
    }

    void setSimpleMode(bool enabled) {
        default_params_.simple_mode = enabled;
        for (auto& [sym, machine] : machines_) {
            machine.setSimpleMode(enabled);
        }
    }

    MicroStateMachine& get(const std::string& symbol) {
        auto it = machines_.find(symbol);
        if (it == machines_.end()) {
            machines_.emplace(symbol, MicroStateMachine());
            auto& m = machines_[symbol];
            m.setParams(default_params_);
            if (symbol == debug_symbol_) {
                m.setDebug(true);
            }
        }
        return machines_[symbol];
    }

    void reset() { 
        machines_.clear(); 
    }

    std::string getDiagnostics(const std::string& symbol) const {
        auto it = machines_.find(symbol);
        if (it == machines_.end()) return "NOT_FOUND";
        const auto& m = it->second;
        char buf[256];
        snprintf(buf, sizeof(buf), 
                 "state=%s veto=%s ticks=%lld dir=%d flips=%d cool=%lldms",
                 stateStr(m.state()), 
                 vetoStr(m.lastVeto()), 
                 (long long)m.totalTicks(),
                 m.lastTradeDirection(),
                 m.flipCount(),
                 (long long)m.cooldownRemainingMs());
        return std::string(buf);
    }

private:
    MicroProfileParams default_params_;
    std::unordered_map<std::string, MicroStateMachine> machines_;
    std::string debug_symbol_ = "XAUUSD";
};

} // namespace Omega
