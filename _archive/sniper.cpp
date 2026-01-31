#include "engines/sniper.hpp"

#include <cmath>

Sniper::Sniper(const std::string& symbol, ChimeraTelemetry& telemetry)
    : symbol_(symbol),
      telemetry_(telemetry),
      has_intent_(false),
      last_mid_(0.0) {}

double Sniper::clamp(double v, double lo, double hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double Sniper::compute_edge(double pullback_bps, double spread_bps, double latency_ms) const {
    double edge = pullback_bps - spread_bps - (latency_ms * 0.1);
    return clamp(edge, -50.0, 50.0);
}

void Sniper::on_tick(const MarketTick& t) {
    double mid = (t.bid + t.ask) * 0.5;
    double pullback = (last_mid_ > 0.0) ? (last_mid_ - mid) : 0.0;
    last_mid_ = mid;

    double pullback_bps = (mid != 0.0) ? (pullback / mid) * 10000.0 : 0.0;
    double spread_bps = (t.ask - t.bid) / mid * 10000.0;
    double latency_ms = 1.0;

    double edge = compute_edge(pullback_bps, spread_bps, latency_ms);

    if (std::fabs(edge) > 4.0) {
        last_intent_.symbol = symbol_;
        last_intent_.price = mid;
        last_intent_.edge  = edge;

        has_intent_ = true;
        telemetry_.trades.fetch_add(1);
    }
}

bool Sniper::has_intent() const {
    return has_intent_;
}

OrderIntent Sniper::take_intent() {
    has_intent_ = false;
    return last_intent_;
}

void Sniper::on_fill(double price, double qty, double pnl_bps) {
    telemetry_.eth_price.store(price);
}
