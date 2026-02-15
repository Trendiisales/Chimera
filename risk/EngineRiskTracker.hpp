#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include "../config/V2Config.hpp"

namespace ChimeraV2 {

struct EngineRiskState {
    int consecutive_losses = 0;
    bool cooldown = false;
    uint64_t cooldown_end_ns = 0;
};

class EngineRiskTracker {
public:
    void record_result(const std::string& key, double pnl, uint64_t now_ns) {
        auto& state = states_[key];

        if (pnl < 0.0) {
            state.consecutive_losses++;
            if (state.consecutive_losses >= V2Config::ENGINE_MAX_CONSEC_LOSSES) {
                state.cooldown = true;
                state.cooldown_end_ns = now_ns +
                    V2Config::ENGINE_COOLDOWN_SECONDS * 1000000000ULL;
                state.consecutive_losses = 0;
            }
        } else {
            state.consecutive_losses = 0;
        }
    }

    bool allowed(const std::string& key, uint64_t now_ns) {
        auto& state = states_[key];
        if (state.cooldown && now_ns < state.cooldown_end_ns)
            return false;

        state.cooldown = false;
        return true;
    }

private:
    std::unordered_map<std::string, EngineRiskState> states_;
};

}
