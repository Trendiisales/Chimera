#include "engines/mean_reversion.hpp"

#include <cmath>

MeanReversion::MeanReversion(const std::string& symbol, ChimeraTelemetry& telemetry)
    : symbol_(symbol),
      telemetry_(telemetry),
      has_intent_(false),
      last_mid_(0.0),
      rolling_vol_(0.0) {}

double MeanReversion::clamp(double v, double lo, double hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double MeanReversion::compute_edge(double deviation_bps, double vol_bps) const {
    double edge = deviation_bps - (vol_bps * 0.5);
    return clamp(edge, -50.0, 50.0);
}

void MeanReversion::on_tick(const MarketTick& t) {
    double mid = (t.bid + t.ask) * 0.5;

    if (last_mid_ == 0.0) {
        last_mid_ = mid;
        return;
    }

    double deviation_bps = (mid - last_mid_) / last_mid_ * 10000.0;

    // EMA-style rolling volatility
    double abs_dev = std::fabs(deviation_bps);
    rolling_vol_ = (rolling_vol_ * 0.9) + (abs_dev * 0.1);

    double edge = compute_edge(deviation_bps, rolling_vol_);

    if (std::fabs(edge) > 6.0) {
        last_intent_.symbol = symbol_;
        last_intent_.price = mid;
        last_intent_.edge  = edge;

        has_intent_ = true;
        telemetry_.trades.fetch_add(1);
    }

    last_mid_ = mid;
}

bool MeanReversion::has_intent() const {
    return has_intent_;
}

OrderIntent MeanReversion::take_intent() {
    has_intent_ = false;
    return last_intent_;
}

void MeanReversion::on_fill(double price, double qty, double pnl_bps) {
    telemetry_.eth_price.store(price);
}
