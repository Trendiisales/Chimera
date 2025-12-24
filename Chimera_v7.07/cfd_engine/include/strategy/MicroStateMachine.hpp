// =============================================================================
// MicroStateMachine.hpp - v6.88 FIXED - Actually Trades Now
// =============================================================================
// CRITICAL FIX: impulse_mult was too high, impulse never detected
// Added: SIMPLE_MODE - bypasses impulse/exhaustion for immediate trading
// Added: Heavy debug logging to see exactly what's happening
// =============================================================================
#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Omega {

enum class MicroState { IDLE, IMPULSE, IN_POSITION, COOLDOWN, LOCKED };
enum class VetoReason { NONE, NO_IMPULSE, NO_EXHAUSTION, COOLDOWN_ACTIVE, DIRECTION_LOCK,
                        CHURN_LOCK, SPREAD_WIDE, NO_EDGE, MICRO_VOL_ZERO, LOCK_EXPIRED, WARMUP };
enum class MicroProfile { CONSERVATIVE, BALANCED, AGGRESSIVE };

inline const char* stateStr(MicroState s) {
    switch(s) { case MicroState::IDLE: return "IDLE"; case MicroState::IMPULSE: return "IMPULSE";
                case MicroState::IN_POSITION: return "IN_POS"; case MicroState::COOLDOWN: return "COOL";
                case MicroState::LOCKED: return "LOCK"; default: return "?"; }
}
inline const char* vetoStr(VetoReason r) {
    switch(r) { case VetoReason::NONE: return "NONE"; case VetoReason::NO_IMPULSE: return "NO_IMP";
                case VetoReason::NO_EXHAUSTION: return "NO_EXH"; case VetoReason::COOLDOWN_ACTIVE: return "COOL";
                case VetoReason::DIRECTION_LOCK: return "DIR_LOCK"; case VetoReason::CHURN_LOCK: return "CHURN";
                case VetoReason::SPREAD_WIDE: return "SPREAD"; case VetoReason::NO_EDGE: return "NO_EDGE";
                case VetoReason::WARMUP: return "WARMUP"; default: return "?"; }
}
inline const char* profStr(MicroProfile p) {
    switch(p) { case MicroProfile::CONSERVATIVE: return "CONS"; case MicroProfile::BALANCED: return "BAL";
                case MicroProfile::AGGRESSIVE: return "AGG"; default: return "?"; }
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
    double impulse_mult = 0.8;      // v6.88: LOWERED from 1.8 - was preventing impulse detection
    double exhaustion_mult = 0.5;
    int exhaustion_ticks = 2;       // v6.88: LOWERED from 3
    int cooldown_ms = 500;          // v6.88: LOWERED from 600
    int min_hold_ms = 300;          // v6.88: LOWERED from 400
    int churn_flip_limit = 5;       // v6.88: RAISED from 4
    int lock_duration_ms = 30000;   // v6.88: LOWERED from 60000
    int warmup_ticks = 30;          // v6.88: LOWERED from 50
    double max_spread_bps = 20.0;   // v6.88: RAISED from 12
    double min_edge_bps = 0.0;      // v6.88: LOWERED from 0.5 - allow any edge
    bool simple_mode = true;        // v6.88: NEW - bypass impulse/exhaustion
    
    static MicroProfileParams balanced() {
        MicroProfileParams p;
        p.impulse_mult = 0.8;
        p.exhaustion_ticks = 2;
        p.cooldown_ms = 500;
        p.min_hold_ms = 300;
        p.warmup_ticks = 30;
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
        
        // Warmup
        if (total_ticks_ < params_.warmup_ticks) {
            last_veto_ = VetoReason::WARMUP;
            if (debug_ && total_ticks_ % 10 == 0) {
                std::cout << "[MICRO] WARMUP " << total_ticks_ << "/" << params_.warmup_ticks << "\n";
            }
            return;
        }
        
        // Lock expiry
        if (state_ == MicroState::LOCKED && in.now_ns >= lock_until_ns_) {
            state_ = MicroState::IDLE;
            if (debug_) std::cout << "[MICRO] LOCK_EXPIRED -> IDLE\n";
        }
        
        // Cooldown expiry
        if (state_ == MicroState::COOLDOWN) {
            int64_t elapsed = (in.now_ns - state_ts_ns_) / NS_PER_MS;
            if (elapsed >= params_.cooldown_ms) {
                state_ = MicroState::IDLE;
                if (debug_) std::cout << "[MICRO] COOLDOWN_DONE -> IDLE\n";
            }
        }
        
        // Simple mode: stay in IDLE, allow trades based on spread/churn only
        if (params_.simple_mode) {
            if (state_ == MicroState::IDLE) {
                last_veto_ = VetoReason::NONE;
            }
            return;
        }
        
        // Full impulse detection mode
        if (state_ == MicroState::IDLE) {
            double disp = std::fabs(in.last_price - in.vwap);
            double thresh = params_.impulse_mult * in.micro_vol;
            
            if (debug_ && total_ticks_ % 50 == 0) {
                std::cout << "[MICRO] disp=" << disp << " thresh=" << thresh 
                          << " vol=" << in.micro_vol << "\n";
            }
            
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
    
    MicroDecision allowEntry(int direction, double spread_bps, double tp_bps) {
        MicroDecision d;
        d.current_state = state_;
        d.impulse_dir = impulse_dir_;
        
        // Debug log EVERY call
        if (debug_) {
            std::cout << "[ALLOW] state=" << stateStr(state_) 
                      << " ticks=" << total_ticks_
                      << " spread=" << spread_bps 
                      << " tp=" << tp_bps 
                      << " simple=" << params_.simple_mode << "\n";
        }
        
        // Warmup check
        if (total_ticks_ < params_.warmup_ticks) {
            d.veto = VetoReason::WARMUP;
            last_veto_ = d.veto;
            return d;
        }
        
        // Locked
        if (state_ == MicroState::LOCKED) {
            d.veto = VetoReason::CHURN_LOCK;
            last_veto_ = d.veto;
            return d;
        }
        
        // Cooldown
        if (state_ == MicroState::COOLDOWN) {
            d.veto = VetoReason::COOLDOWN_ACTIVE;
            last_veto_ = d.veto;
            return d;
        }
        
        // Already in position
        if (state_ == MicroState::IN_POSITION) {
            d.veto = VetoReason::COOLDOWN_ACTIVE;
            last_veto_ = d.veto;
            return d;
        }
        
        // SIMPLE MODE: Skip impulse/exhaustion checks
        if (params_.simple_mode) {
            // Only check spread and churn
            if (spread_bps > params_.max_spread_bps) {
                d.veto = VetoReason::SPREAD_WIDE;
                last_veto_ = d.veto;
                if (debug_) std::cout << "[ALLOW] VETO spread " << spread_bps << " > " << params_.max_spread_bps << "\n";
                return d;
            }
            
            // Check churn
            if (direction_flips_30s_ >= params_.churn_flip_limit) {
                d.veto = VetoReason::CHURN_LOCK;
                last_veto_ = d.veto;
                return d;
            }
            
            // ALLOW THE TRADE
            d.allow_trade = true;
            d.veto = VetoReason::NONE;
            last_veto_ = VetoReason::NONE;
            if (debug_) std::cout << "[ALLOW] *** TRADE ALLOWED ***\n";
            return d;
        }
        
        // FULL MODE: Require impulse state
        if (state_ != MicroState::IMPULSE) {
            d.veto = VetoReason::NO_IMPULSE;
            last_veto_ = d.veto;
            return d;
        }
        
        // Direction check
        if (direction != impulse_dir_) {
            d.veto = VetoReason::DIRECTION_LOCK;
            last_veto_ = d.veto;
            return d;
        }
        
        // Exhaustion check
        if (exhaustion_ticks_ < params_.exhaustion_ticks) {
            d.veto = VetoReason::NO_EXHAUSTION;
            last_veto_ = d.veto;
            return d;
        }
        
        // Spread check
        if (spread_bps > params_.max_spread_bps) {
            d.veto = VetoReason::SPREAD_WIDE;
            last_veto_ = d.veto;
            return d;
        }
        
        // Edge check
        if (tp_bps < spread_bps + params_.min_edge_bps) {
            d.veto = VetoReason::NO_EDGE;
            last_veto_ = d.veto;
            return d;
        }
        
        d.allow_trade = true;
        d.veto = VetoReason::NONE;
        last_veto_ = VetoReason::NONE;
        if (debug_) std::cout << "[ALLOW] *** TRADE ALLOWED ***\n";
        return d;
    }
    
    void onEntry(int direction, int64_t now_ns) {
        if (debug_) std::cout << "[MICRO] >>> ON_ENTRY dir=" << direction << " <<<\n";
        
        // Track direction flips for churn
        if (last_direction_ != 0 && direction != last_direction_) {
            direction_flips_30s_++;
            if (debug_) std::cout << "[MICRO] FLIP! count=" << direction_flips_30s_ << "\n";
        }
        last_direction_ = direction;
        
        state_ = MicroState::IN_POSITION;
        state_ts_ns_ = now_ns;
        
        // Check churn lock
        if (direction_flips_30s_ >= params_.churn_flip_limit) {
            state_ = MicroState::LOCKED;
            lock_until_ns_ = now_ns + params_.lock_duration_ms * NS_PER_MS;
            if (debug_) std::cout << "[MICRO] CHURN_LOCK! flips=" << direction_flips_30s_ << "\n";
        }
    }
    
    void onExit(int64_t now_ns) {
        if (debug_) std::cout << "[MICRO] >>> ON_EXIT <<<\n";
        state_ = MicroState::COOLDOWN;
        state_ts_ns_ = now_ns;
    }
    
    bool canExit(int64_t now_ns) const {
        if (state_ != MicroState::IN_POSITION) return true;
        return (now_ns - state_ts_ns_) / NS_PER_MS >= params_.min_hold_ms;
    }
    
    // Getters
    MicroState state() const { return state_; }
    VetoReason lastVeto() const { return last_veto_; }
    int64_t totalTicks() const { return total_ticks_; }
    int impulseDirection() const { return impulse_dir_; }
    const MicroProfileParams& params() const { return params_; }
    
    void reset() {
        state_ = MicroState::IDLE;
        total_ticks_ = 0;
        direction_flips_30s_ = 0;
        last_direction_ = 0;
    }

private:
    MicroProfileParams params_;
    MicroState state_ = MicroState::IDLE;
    VetoReason last_veto_ = VetoReason::NONE;
    
    int64_t total_ticks_ = 0;
    int64_t state_ts_ns_ = 0;
    int64_t lock_until_ns_ = 0;
    
    int impulse_dir_ = 0;
    int exhaustion_ticks_ = 0;
    double prev_price_ = 0;
    
    int direction_flips_30s_ = 0;
    int last_direction_ = 0;
    
    MicroInputs last_input_;
    bool debug_ = false;
};

// =============================================================================
// MULTI-SYMBOL MANAGER
// =============================================================================
class MicroStateManager {
public:
    MicroStateManager() {
        // Enable simple mode by default for all symbols
        default_params_ = MicroProfileParams::balanced();
        default_params_.simple_mode = true;
    }
    
    void setDebugSymbol(const std::string& sym) {
        debug_symbol_ = sym;
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
                std::cout << "[MICRO] Debug enabled for " << symbol << "\n";
            }
        }
        return machines_[symbol];
    }
    
    void reset() { machines_.clear(); }
    
    std::string getDiagnostics(const std::string& symbol) const {
        auto it = machines_.find(symbol);
        if (it == machines_.end()) return "NOT_FOUND";
        const auto& m = it->second;
        char buf[128];
        snprintf(buf, sizeof(buf), "state=%s veto=%s ticks=%lld",
                 stateStr(m.state()), vetoStr(m.lastVeto()), (long long)m.totalTicks());
        return std::string(buf);
    }

private:
    MicroProfileParams default_params_;
    std::unordered_map<std::string, MicroStateMachine> machines_;
    std::string debug_symbol_ = "XAUUSD";
};

} // namespace Omega
