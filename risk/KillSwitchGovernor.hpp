#pragma once

#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <iostream>

class KillSwitchGovernor {
public:
    KillSwitchGovernor() = default;

    inline void registerEngine(const std::string& name) {
        std::lock_guard<std::mutex> g(mu_);
        engines_[name] = true;
    }

    inline void recordSignal(const std::string& engine, uint64_t) {
        std::lock_guard<std::mutex> g(mu_);
        if (engines_.count(engine)) {
            last_engine_ = engine;
        }
    }

    inline bool globalEnabled() const {
        return global_enabled_.load(std::memory_order_relaxed);
    }

    inline bool isEngineEnabled(const std::string& engine) const {
        auto it = engines_.find(engine);
        if (it == engines_.end()) return false;
        return it->second && global_enabled_.load(std::memory_order_relaxed);
    }

    inline double scaleSize(const std::string&, double raw) const {
        return raw * risk_scale_.load(std::memory_order_relaxed);
    }

    inline void setGlobalEnabled(bool v) {
        bool prev = global_enabled_.exchange(v, std::memory_order_relaxed);
        if (prev != v) {
            if (!v) {
                std::cerr << "[RISK] GLOBAL FREEZE ENABLED\n";
            } else {
                std::cerr << "[RISK] GLOBAL TRADING RESUMED\n";
            }
        }
    }

    inline void setRiskScale(double v) {
        double prev = risk_scale_.exchange(v, std::memory_order_relaxed);
        if (prev != v) {
            std::cerr << "[RISK] SCALE -> " << v << "\n";
        }
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, bool> engines_;
    std::string last_engine_;

    std::atomic<bool> global_enabled_{true};
    std::atomic<double> risk_scale_{1.0};
};
