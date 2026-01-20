#pragma once

#include <atomic>
#include "../tier3/TickData.hpp"

struct CrossSignal {
    bool is_buy;
    double confidence;
    double price;
};

class CrossSymbolRunner {
public:
    CrossSymbolRunner() {}

    void onBtcTick(const tier3::TickData& t) {
        double px = t.midprice();
        double ofi = t.ofi_z;

        double dpx = px - last_px_;
        double dofi = ofi - last_ofi_;

        last_px_ = px;
        last_ofi_ = ofi;

        bool impulse =
            (t.btc_impulse > 0) ||
            (t.liquidation_long || t.liquidation_short) ||
            (t.impulse_bps > 8.0f && dofi > 0.5f);

        if (!impulse) return;
        if (t.spread_bps > 12.0f) return;
        if (t.depth_ratio < 0.6f) return;

        CrossSignal sig;
        sig.is_buy = (dpx > 0);
        sig.price = px;
        sig.confidence = clamp(
            (t.impulse_bps * 0.15) +
            (dofi * 0.25) +
            (t.depth_ratio * 0.5),
            0.0,
            1.0
        );

        signal_ = sig;
        has_signal_.store(true, std::memory_order_release);
    }

    void onEthTick(const tier3::TickData& t) {
        last_eth_px_ = t.midprice();
    }

    bool hasSignal() const {
        return has_signal_.load(std::memory_order_acquire);
    }

    CrossSignal consumeSignal() {
        has_signal_.store(false, std::memory_order_release);
        return signal_;
    }

private:
    double clamp(double v, double lo, double hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    double last_px_{0};
    double last_ofi_{0};
    double last_eth_px_{0};

    CrossSignal signal_;
    std::atomic<bool> has_signal_{false};
};
