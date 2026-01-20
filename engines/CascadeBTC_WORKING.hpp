#pragma once

#include <atomic>
#include "../tier3/TickData.hpp"

struct BtcSignal {
    bool is_buy;
    double confidence;
    double price;
};

class CascadeBTC_WORKING {
public:
    CascadeBTC_WORKING() {}

    void onTick(const tier3::TickData& t) {
        double px = t.midprice();
        double ofi = t.ofi_z;

        double dpx = px - last_px_;
        double dofi = ofi - last_ofi_;

        last_px_ = px;
        last_ofi_ = ofi;

        bool cascade =
            (t.liquidation_long || t.liquidation_short) ||
            (t.btc_impulse > 0) ||
            (t.impulse_bps > 10.0f && std::abs(dofi) > 0.6f);

        if (!cascade) return;

        if (t.spread_bps > 15.0f) return;
        if (t.depth_ratio < 0.5f) return;

        BtcSignal sig;
        sig.is_buy = (dpx > 0);
        sig.price = px;
        sig.confidence = clamp(
            (t.impulse_bps * 0.2) +
            (std::abs(dofi) * 0.3) +
            (t.depth_ratio * 0.3),
            0.0,
            1.0
        );

        signal_ = sig;
        has_signal_.store(true, std::memory_order_release);
    }

    bool hasSignal() const {
        return has_signal_.load(std::memory_order_acquire);
    }

    BtcSignal consumeSignal() {
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

    BtcSignal signal_;
    std::atomic<bool> has_signal_{false};
};
