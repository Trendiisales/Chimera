#pragma once
#include "IEngine.hpp"
#include "../config/V2Config.hpp"
#include <unordered_map>
#include <chrono>
#include <cmath>

namespace ChimeraV2 {

class MicroImpulseEngine : public IEngine {
public:
    MicroImpulseEngine() : id_(4) {}

    int id() const override { return id_; }
    const char* name() const override { return "MicroImpulse"; }

    V2Proposal evaluate(const SymbolState& s) override {
        V2Proposal p;
        p.engine_id = id_;
        p.symbol = s.symbol;
        p.timestamp_ns = s.timestamp_ns;

        if (blocked_) return p;

        uint64_t now_ms = s.timestamp_ns / 1000000ULL;
        auto& state = symbol_state_[s.symbol];

        // Cooldown gate
        if (now_ms - state.last_trade_ms < V2Config::XAU_COOLDOWN_MS)
            return p;

        // Trade cap gate
        if (state.trades_this_hour >= max_trades_per_hour_)
            return p;

        // Loss block gate
        if (state.loss_count >= V2Config::XAU_BLOCK_ON_LOSS_COUNT)
            return p;

        // Latency gate (using velocity as proxy for now - real latency needs feed integration)
        double latency_proxy = std::abs(s.velocity) * 100.0;
        int rtt_max = (s.symbol == "XAUUSD") ? V2Config::XAU_ENTRY_RTT_MAX_MS : V2Config::XAG_ENTRY_RTT_MAX_MS;
        if (latency_proxy > rtt_max)
            return p;

        // Impulse threshold (using velocity as impulse proxy)
        double impulse = std::abs(s.velocity);
        double min_impulse = V2Config::XAU_MIN_IMPULSE_FAST;
        
        // Session-aware threshold (using structural_momentum as regime proxy)
        if (std::abs(s.structural_momentum) > 0.1) {
            min_impulse = V2Config::XAU_MIN_IMPULSE_OPEN;
        }

        if (impulse < min_impulse)
            return p;

        // Valid proposal
        p.valid = true;
        p.side = (s.velocity > 0.0) ? Side::BUY : Side::SELL;
        p.size = 1.0;
        p.structural_score = impulse;
        p.confidence = 0.7;

        // V1 PROVEN: Fixed stop per symbol
        p.estimated_risk = (s.symbol == "XAUUSD") 
            ? V2Config::XAU_STOP_DISTANCE 
            : V2Config::XAG_STOP_DISTANCE;

        // Store entry state for decay tracking
        state.entry_impulse = impulse;
        state.entry_ts_ms = now_ms;
        last_symbol_ = s.symbol;

        return p;
    }

    void on_trade_closed(double pnl, double R) override {
        (void)R;
        
        auto& state = symbol_state_[last_symbol_];

        state.trades_this_hour++;
        state.last_trade_ms = current_time_ms();

        if (pnl < 0.0) {
            state.loss_count++;
            if (state.loss_count >= V2Config::XAU_BLOCK_ON_LOSS_COUNT) {
                blocked_ = true;
                block_until_ms_ = current_time_ms() + (V2Config::ENGINE_COOLDOWN_SECONDS * 1000ULL);
            }
        } else {
            state.loss_count = 0;
        }

        // Check if block expired
        if (blocked_ && current_time_ms() >= block_until_ms_) {
            blocked_ = false;
            for (auto& pair : symbol_state_) {
                pair.second.loss_count = 0;
            }
        }
    }

private:
    struct SymbolState {
        uint64_t last_trade_ms = 0;
        uint64_t entry_ts_ms = 0;
        double entry_impulse = 0.0;
        int trades_this_hour = 0;
        int loss_count = 0;
    };

    uint64_t current_time_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    int id_;
    bool blocked_ = false;
    uint64_t block_until_ms_ = 0;

    static constexpr int max_trades_per_hour_ = 20;  // V1 XAU limit

    std::unordered_map<std::string, SymbolState> symbol_state_;
    std::string last_symbol_;
};

}
