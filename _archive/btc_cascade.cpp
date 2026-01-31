#include "engines/btc_cascade.hpp"

#include <cmath>

BTCCascade::BTCCascade(const std::string& symbol, ChimeraTelemetry& telemetry)
    : symbol_(symbol),
      telemetry_(telemetry),
      has_intent_(false),
      last_mid_(0.0) {}

double BTCCascade::clamp(double v, double lo, double hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double BTCCascade::compute_edge(double ofi_z, double impulse_bps) const {
    double edge = ofi_z * 0.6 + impulse_bps * 0.4;
    return clamp(edge, -50.0, 50.0);
}

void BTCCascade::on_tick(const MarketTick& t) {
    double mid = (t.bid + t.ask) * 0.5;
    double impulse = (last_mid_ > 0.0) ? (mid - last_mid_) : 0.0;
    last_mid_ = mid;

    double impulse_bps = (mid != 0.0) ? (impulse / mid) * 10000.0 : 0.0;
    double ofi_z = impulse_bps * 0.5;

    double edge = compute_edge(ofi_z, impulse_bps);

    if (std::fabs(edge) > 5.0) {
        last_intent_.symbol = symbol_;
        last_intent_.price = mid;
        last_intent_.edge  = edge;

        has_intent_ = true;
        telemetry_.trades.fetch_add(1);
    }
}

bool BTCCascade::has_intent() const {
    return has_intent_;
}

OrderIntent BTCCascade::take_intent() {
    has_intent_ = false;
    return last_intent_;
}

void BTCCascade::on_fill(double price, double qty, double pnl_bps) {
    telemetry_.btc_price.store(price);
}
