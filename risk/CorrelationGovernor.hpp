#pragma once

#include <atomic>
#include <string>

class CorrelationGovernor {
public:
    CorrelationGovernor() = default;

    // MARKET STRESS METRICS
    inline double btcStress() const {
        return btc_stress_.load(std::memory_order_relaxed);
    }

    inline double ethStress() const {
        return eth_stress_.load(std::memory_order_relaxed);
    }

    // SETTERS (FEED / ENGINE SIDE)
    inline void setBtcStress(double v) {
        btc_stress_.store(v, std::memory_order_relaxed);
    }

    inline void setEthStress(double v) {
        eth_stress_.store(v, std::memory_order_relaxed);
    }

    // GLOBAL CORRELATION GATE
    inline bool allowTrade(const std::string&, bool) const {
        return true;
    }

private:
    std::atomic<double> btc_stress_{0.0};
    std::atomic<double> eth_stress_{0.0};
};
