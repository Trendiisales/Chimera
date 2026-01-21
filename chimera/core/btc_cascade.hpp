#ifndef BTC_CASCADE_HPP
#define BTC_CASCADE_HPP

#include "engine_signal.hpp"
#include "ofi_engine.hpp"
#include "depth_engine.hpp"
#include "liquidation_engine.hpp"
#include "impulse_engine.hpp"
#include "signal_bridge.hpp"
#include "event_bus.hpp"
#include <atomic>

enum class CascadeState {
    IDLE,
    ARMED,
    IN_TRADE,
    COOLDOWN
};

inline const char* stateStr(CascadeState s) {
    switch (s) {
        case CascadeState::IDLE: return "IDLE";
        case CascadeState::ARMED: return "ARMED";
        case CascadeState::IN_TRADE: return "IN_TRADE";
        case CascadeState::COOLDOWN: return "COOLDOWN";
    }
    return "UNKNOWN";
}

class BTCCascade {
public:
    BTCCascade(
        OFIEngine& ofi,
        DepthEngine& depth,
        LiquidationEngine& liq,
        ImpulseEngine& impulse,
        SignalBridge& bridge,
        EventBus<CascadeEvent>& bus
    ) : ofi_(ofi), depth_(depth), liq_(liq), impulse_(impulse),
        bridge_(bridge), bus_(bus) {}

    CascadeSignal evaluate(uint64_t now_ns, double spread_bps) {
        CascadeSignal result;
        result.ts_ns = now_ns;
        
        if (state_.load(std::memory_order_acquire) == CascadeState::COOLDOWN) {
            uint64_t cooldown_start = cooldown_start_.load(std::memory_order_relaxed);
            if (now_ns - cooldown_start < cooldown_ns_) {
                return result;
            }
            state_.store(CascadeState::IDLE, std::memory_order_release);
        }
        
        if (state_.load(std::memory_order_acquire) == CascadeState::IN_TRADE) {
            uint64_t entry = entry_ts_.load(std::memory_order_relaxed);
            if (now_ns - entry > max_hold_ns_) {
                state_.store(CascadeState::COOLDOWN, std::memory_order_release);
                cooldown_start_.store(now_ns, std::memory_order_relaxed);
            }
            return result;
        }
        
        if (bridge_.btcBlocked(now_ns)) {
            return result;
        }
        
        OFISignal ofi_sig = ofi_.evaluate(now_ns);
        DepthSignal depth_sig = depth_.evaluate(now_ns);
        LiqSignal liq_sig = liq_.evaluate(now_ns);
        ImpulseSignal impulse_sig = impulse_.evaluate(now_ns);
        
        result.ofi_confirmed = ofi_sig.fired;
        result.depth_confirmed = depth_sig.fired;
        result.liq_confirmed = liq_sig.fired;
        result.impulse_confirmed = impulse_sig.fired;
        
        result.confirmation_count = 
            (ofi_sig.fired ? 1 : 0) +
            (depth_sig.fired ? 1 : 0) +
            (liq_sig.fired ? 1 : 0) +
            (impulse_sig.fired ? 1 : 0);
        
        if (spread_bps > max_spread_bps_) {
            return result;
        }
        
        Side consensus = Side::NONE;
        int direction_votes = 0;
        
        if (ofi_sig.fired && ofi_sig.side != Side::NONE) {
            consensus = ofi_sig.side;
            direction_votes++;
        }
        if (liq_sig.fired && liq_sig.side != Side::NONE) {
            if (consensus == Side::NONE) {
                consensus = liq_sig.side;
            } else if (consensus == liq_sig.side) {
                direction_votes++;
            } else {
                return result;
            }
        }
        if (impulse_sig.fired && impulse_sig.side != Side::NONE) {
            if (consensus == Side::NONE) {
                consensus = impulse_sig.side;
            } else if (consensus == impulse_sig.side) {
                direction_votes++;
            } else {
                return result;
            }
        }
        
        bool should_fire = false;
        
        if (liq_sig.fired && depth_sig.fired && ofi_sig.fired) {
            should_fire = true;
        }
        else if (liq_sig.fired && impulse_sig.fired && consensus != Side::NONE) {
            should_fire = true;
        }
        else if (depth_sig.fired && ofi_sig.fired && impulse_sig.fired) {
            should_fire = true;
        }
        else if (result.confirmation_count >= min_confirmations_ && consensus != Side::NONE) {
            should_fire = true;
        }
        
        if (should_fire && consensus != Side::NONE) {
            result.fired = true;
            result.side = consensus;
            
            last_signal_ = result;
            should_trade_.store(true, std::memory_order_release);
            
            bridge_.blockFollowers(now_ns + follower_block_ns_);
            
            CascadeEvent ev;
            ev.side = consensus;
            ev.ts_ns = now_ns;
            ev.strength = result.confirmation_count / 4.0;
            ev.depth_ratio = depth_.depthRatio();
            ev.ofi_zscore = ofi_.zscore();
            ev.ofi_accel = ofi_.accel();
            ev.forced_flow = ofi_sig.fired || liq_sig.fired;
            
            bus_.publish(ev);
        }
        
        return result;
    }

    bool shouldTrade() const {
        return should_trade_.load(std::memory_order_acquire);
    }

    CascadeSignal lastSignal() const {
        return last_signal_;
    }

    void markExecuted() {
        should_trade_.store(false, std::memory_order_release);
        state_.store(CascadeState::IN_TRADE, std::memory_order_release);
        entry_ts_.store(last_signal_.ts_ns, std::memory_order_relaxed);
    }

    void markExit() {
        state_.store(CascadeState::COOLDOWN, std::memory_order_release);
        cooldown_start_.store(last_signal_.ts_ns, std::memory_order_relaxed);
    }

    CascadeState state() const {
        return state_.load(std::memory_order_acquire);
    }

    void setMinConfirmations(int n) { min_confirmations_ = n; }
    void setMaxSpread(double bps) { max_spread_bps_ = bps; }
    void setMaxHold(uint64_t ns) { max_hold_ns_ = ns; }
    void setCooldown(uint64_t ns) { cooldown_ns_ = ns; }
    void setFollowerBlock(uint64_t ns) { follower_block_ns_ = ns; }

private:
    OFIEngine& ofi_;
    DepthEngine& depth_;
    LiquidationEngine& liq_;
    ImpulseEngine& impulse_;
    SignalBridge& bridge_;
    EventBus<CascadeEvent>& bus_;
    
    std::atomic<CascadeState> state_{CascadeState::IDLE};
    std::atomic<bool> should_trade_{false};
    std::atomic<uint64_t> entry_ts_{0};
    std::atomic<uint64_t> cooldown_start_{0};
    
    CascadeSignal last_signal_;
    
    int min_confirmations_ = 3;
    double max_spread_bps_ = 5.0;
    uint64_t max_hold_ns_ = 30'000'000'000ULL;
    uint64_t cooldown_ns_ = 5'000'000'000ULL;
    uint64_t follower_block_ns_ = 500'000'000ULL;
};

#endif
