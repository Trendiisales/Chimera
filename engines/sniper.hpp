#pragma once
#include "core/contract.hpp"

class Sniper : public IEngine {
    ChimeraTelemetry& telemetry_;
    bool has_intent_ = false;
    OrderIntent last_;

public:
    Sniper(const std::string& sym, ChimeraTelemetry& t)
        : telemetry_(t) {
        last_.symbol = sym;
    }

    void on_tick(const MarketTick& t) override {
        double spread = t.ask - t.bid;
        telemetry_.eth_price = (t.ask + t.bid) * 0.5;

        if (spread < 0.05) {
            last_.is_buy = true;
            last_.price = t.ask;
            last_.qty = 0.01;
            last_.edge = 3.0;
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
