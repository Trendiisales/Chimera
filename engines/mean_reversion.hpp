#pragma once

#include <string>

#include "core/types.hpp"
#include "core/telemetry.hpp"
#include "core/spine.hpp"

class MeanReversion : public IEngine {
public:
    MeanReversion(const std::string& symbol, ChimeraTelemetry& telemetry);

    void on_tick(const MarketTick& t) override;
    bool has_intent() const override;
    OrderIntent take_intent() override;
    void on_fill(double price, double qty, double pnl_bps) override;

private:
    double clamp(double v, double lo, double hi) const;
    double compute_edge(double deviation_bps, double vol_bps) const;

private:
    std::string symbol_;
    ChimeraTelemetry& telemetry_;

    bool has_intent_;
    OrderIntent last_intent_;

    double last_mid_;
    double rolling_vol_;
};
