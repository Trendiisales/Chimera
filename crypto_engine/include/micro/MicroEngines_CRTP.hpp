// ============================================================================
// crypto_engine/include/micro/MicroEngines_CRTP.hpp
// ============================================================================
// Production-grade CRTP micro engines for Binance
// ============================================================================
#pragma once

#include <cstdint>

template<typename Derived>
struct MicroEngineCRTP {
    inline Derived& self() {
        return static_cast<Derived&>(*this);
    }
};

struct OrderBookImbalanceEngine : MicroEngineCRTP<OrderBookImbalanceEngine> {
    double value_ = 0.0;

    inline void on_book(double bid_sz, double ask_sz) {
        const double d = bid_sz + ask_sz;
        value_ = d > 0 ? (bid_sz - ask_sz) / d : 0.0;
    }

    inline double value() const { return value_; }
};

struct MicropriceEngine : MicroEngineCRTP<MicropriceEngine> {
    double value_ = 0.0;

    inline void on_book(
        double bid_px, double bid_sz,
        double ask_px, double ask_sz
    ) {
        const double d = bid_sz + ask_sz;
        value_ = d > 0 ? (bid_px * ask_sz + ask_px * bid_sz) / d : 0.0;
    }

    inline double value() const { return value_; }
};

struct TradeFlowImbalanceEngine : MicroEngineCRTP<TradeFlowImbalanceEngine> {
    double buy_ = 0.0;
    double sell_ = 0.0;
    double value_ = 0.0;

    inline void on_trade(bool is_buy, double qty) {
        is_buy ? buy_ += qty : sell_ += qty;
        const double d = buy_ + sell_;
        if (d > 0) value_ = (buy_ - sell_) / d;
    }

    inline double value() const { return value_; }
};

struct VolatilityBurstEngine : MicroEngineCRTP<VolatilityBurstEngine> {
    double last_px_ = 0.0;
    double ema_var_ = 0.0;
    double burst_ = 0.0;

    inline void on_price(double px) {
        if (last_px_ > 0.0) {
            const double r = px - last_px_;
            const double v = r * r;
            ema_var_ = 0.1 * v + 0.9 * ema_var_;
            burst_ = v - ema_var_;
        }
        last_px_ = px;
    }

    inline double value() const { return burst_; }
};

struct BinanceMicroEngines {
    OrderBookImbalanceEngine obi;
    MicropriceEngine microprice;
    TradeFlowImbalanceEngine tfi;
    VolatilityBurstEngine vol;

    inline void on_book(
        double bid_px, double bid_sz,
        double ask_px, double ask_sz,
        uint64_t
    ) {
        obi.on_book(bid_sz, ask_sz);
        microprice.on_book(bid_px, bid_sz, ask_px, ask_sz);
    }

    inline void on_trade(bool is_buy, double qty, uint64_t) {
        tfi.on_trade(is_buy, qty);
    }

    inline void on_price(double px, uint64_t) {
        vol.on_price(px);
    }
};
