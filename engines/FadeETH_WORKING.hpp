#pragma once

#include <atomic>
#include "../tier3/TickData.hpp"

struct EthSignal {
    bool is_buy;
    double confidence;
    double price;
};

class FadeETH_WORKING {
public:
    FadeETH_WORKING() {}

    void onTick(const tier3::TickData& t) {
        double px = t.midprice();
        double ofi = t.ofi_z;

        double dpx = px - last_px_;
        double dofi = ofi - last_ofi_;

        last_px_ = px;
        last_ofi_ = ofi;

        // Exhaustion logic
        bool exhaustion =
            (t.spread_bps > 6.0f) &&
            (t.depth_ratio < 0.7f) &&
            (std::abs(dofi) > 0.4f);

        if (!exhaustion) return;

        EthSignal sig;
        sig.is_buy = (dpx < 0); // fade move
        sig.price = px;
        sig.confidence = clamp(
            (std::abs(dofi) * 0.4) +
            ((1.0 - t.depth_ratio) * 0.4) +
            (t.spread_bps * 0.05),
            0.0,
            1.0
        );

        signal_ = sig;
        has_signal_.store(true, std::memory_order_release);
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
    double clamp(double v, double lo, double hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    double last_px_{0};
    double last_ofi_{0};

    EthSignal signal_;
    std::atomic<bool> has_signal_{false};
};
