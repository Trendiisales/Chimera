#!/usr/bin/env bash
set -e

ROOT="$(pwd)"
[ -f "$ROOT/CMakeLists.txt" ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "======================================"
echo "CHIMERA MEAN REVERSION LOCK"
echo "HEADER / CPP SYNC"
echo "======================================"

############################################
# MEAN REVERSION HEADER
############################################
cat << 'HPP' > engines/mean_reversion.hpp
#pragma once
#include <string>
#include "core/types.hpp"
#include "core/telemetry.hpp"

class MeanReversion {
public:
    MeanReversion(const std::string& symbol, ChimeraTelemetry& telemetry);

    void on_tick(const MarketTick& t);

    bool has_intent() const;
    OrderIntent take_intent();

    void on_fill(double price, double qty, double pnl_bps);

private:
    double clamp(double v, double lo, double hi) const;
    double compute_vwap() const;
    double compute_volatility() const;
    double compute_edge(double deviation_bps, double vol_bps) const;

private:
    std::string symbol_;
    ChimeraTelemetry& telemetry_;
    bool has_intent_;
    OrderIntent last_intent_;

    double vwap_;
    double last_price_;
    double vol_;
};
HPP

############################################
# MEAN REVERSION IMPLEMENTATION
############################################
cat << 'CPP' > engines/mean_reversion.cpp
#include "engines/mean_reversion.hpp"
#include <cmath>

MeanReversion::MeanReversion(const std::string& symbol, ChimeraTelemetry& telemetry)
    : symbol_(symbol),
      telemetry_(telemetry),
      has_intent_(false),
      vwap_(0.0),
      last_price_(0.0),
      vol_(0.0) {
}

double MeanReversion::clamp(double v, double lo, double hi) const {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double MeanReversion::compute_vwap() const {
    return last_price_;
}

double MeanReversion::compute_volatility() const {
    return vol_;
}

double MeanReversion::compute_edge(double deviation_bps, double vol_bps) const {
    double raw = deviation_bps - vol_bps * 0.5;
    return clamp(raw, -50.0, 50.0);
}

void MeanReversion::on_tick(const MarketTick& t) {
    telemetry_.eth_price = t.ask;
    telemetry_.trades++;

    last_price_ = (t.bid + t.ask) * 0.5;

    double mid = last_price_;
    vwap_ = compute_vwap();

    double deviation_bps = (mid - vwap_) * 100.0;
    vol_ = std::fabs(deviation_bps) * 0.2;

    double edge = compute_edge(deviation_bps, vol_);

    if (std::fabs(edge) > 6.0) {
        last_intent_.symbol = symbol_;
        last_intent_.side = edge > 0 ? "SELL" : "BUY";
        last_intent_.price = edge > 0 ? t.bid : t.ask;
        last_intent_.size = 0.01;
        last_intent_.edge = edge;
        has_intent_ = true;
    }
}

bool MeanReversion::has_intent() const {
    return has_intent_;
}

OrderIntent MeanReversion::take_intent() {
    has_intent_ = false;
    return last_intent_;
}

void MeanReversion::on_fill(double price, double qty, double pnl_bps) {
    (void)price;
    (void)qty;
    (void)pnl_bps;
}
CPP

############################################
# CLEAN BUILD
############################################
rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)

echo "======================================"
echo "MEAN REVERSION LOCK COMPLETE"
echo "NO HEADER / CPP DRIFT"
echo "======================================"
