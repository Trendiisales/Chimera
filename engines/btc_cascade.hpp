#pragma once
#include "core/contract.hpp"

class BTCCascade : public IEngine {
    ChimeraTelemetry& telemetry_;
    bool has_intent_ = false;
    OrderIntent last_;
    double last_mid_ = 0.0;

public:
    BTCCascade(const std::string& sym, ChimeraTelemetry& t)
        : telemetry_(t) {
        last_.symbol = sym;
    }

    void on_tick(const MarketTick& t) override {
        double mid = (t.bid + t.ask) * 0.5;
        telemetry_.btc_price = mid;

        if (last_mid_ == 0.0) {
            last_mid_ = mid;
            return;
        }

        double move_bps = (mid - last_mid_) / last_mid_ * 10000.0;
        last_mid_ = mid;

        if (move_bps > 5.0) {
            last_.is_buy = true;
            last_.price = t.ask;
            last_.qty = 0.001;
            last_.edge = move_bps;
            has_intent_ = true;
        }
    }

    bool has_intent() const override {
        return has_intent_;
    }

    OrderIntent take_intent() override {
        has_intent_ = false;
        return last_;
    }

    void on_fill(const FillEvent&) override {}
};
