#pragma once

#include "../tier3/TickData.hpp"
#include <atomic>

struct EthSignal {
    bool is_buy;
    double confidence;
    double price;
};

class FadeETH_SIMPLE {
public:
    FadeETH_SIMPLE() = default;

    void onTick(const tier3::TickData& t) {
        double px = t.midprice();
        double ofi = t.ofi_z;

        double accel = ofi - last_ofi_;
        last_ofi_ = ofi;

        if (accel > 0.5) {
            signal_ = {true, accel, px};
            has_signal_.store(true, std::memory_order_release);
        } else if (accel < -0.5) {
            signal_ = {false, -accel, px};
            has_signal_.store(true, std::memory_order_release);
        }
    }

    bool hasSignal() const {
        return has_signal_.load(std::memory_order_acquire);
    }

    EthSignal consumeSignal() {
        has_signal_.store(false, std::memory_order_release);
        return signal_;
    }

    void onTickExit(const tier3::TickData&) {}

private:
    double last_ofi_{0.0};
    EthSignal signal_{};
    std::atomic<bool> has_signal_{false};
};
