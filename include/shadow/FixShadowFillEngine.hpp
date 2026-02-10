#pragma once
#include <cmath>
#include <cstdint>

namespace shadow {

enum class ExecSide { BUY = 1, SELL = -1 };

struct FillResult {
    bool filled = false;
    double fill_price = 0.0;
    bool taker = false;
};

struct FixShadowFillEngine {
    // ---- CONFIG ----
    double tick_size;
    double avg_queue_ahead;
    double avg_trade_rate;
    double taker_slip_ticks;
    double latency_ms;

    // ---- STATE ----
    double queue_remaining = 0.0;
    bool active = false;

    void reset() {
        queue_remaining = 0.0;
        active = false;
    }

    void submit_maker(double size) {
        queue_remaining = avg_queue_ahead + size;
        active = true;
    }

    FillResult on_tick(double traded_volume,
                       double best_price,
                       ExecSide side) {
        FillResult r;
        if (!active) return r;

        queue_remaining -= traded_volume;
        if (queue_remaining <= 0.0) {
            r.filled = true;
            r.fill_price = best_price;
            r.taker = false;
            active = false;
        }
        return r;
    }

    FillResult force_taker(double best_price, ExecSide side) const {
        FillResult r;
        r.filled = true;
        r.taker = true;
        r.fill_price =
            best_price + (int)side * taker_slip_ticks * tick_size;
        return r;
    }
};

// XAU defaults (from Documents 8-9)
inline FixShadowFillEngine getXauFillEngine() {
    return {
        0.01,   // tick_size
        120.0,  // avg_queue_ahead
        6.0,    // avg_trade_rate
        2.5,    // taker_slip_ticks
        10.0    // latency_ms
    };
}

inline FixShadowFillEngine getXagFillEngine() {
    return {
        0.001,  // tick_size
        80.0,   // avg_queue_ahead
        4.0,    // avg_trade_rate
        3.0,    // taker_slip_ticks
        12.0    // latency_ms
    };
}

inline FixShadowFillEngine getNasFillEngine() {
    return {
        0.25,   // tick_size
        200.0,  // avg_queue_ahead
        10.0,   // avg_trade_rate
        1.5,    // taker_slip_ticks
        8.0     // latency_ms
    };
}

inline FixShadowFillEngine getUs30FillEngine() {
    return {
        1.0,    // tick_size
        150.0,  // avg_queue_ahead
        8.0,    // avg_trade_rate
        2.0,    // taker_slip_ticks
        8.0     // latency_ms
    };
}

// Microstructure guard (from Document 8-9)
struct MicrostructureGuard {
    // ---- THRESHOLDS ----
    double max_latency_ms = 15.0;
    double min_fill_ratio = 0.35;
    double max_spread_ticks = 4.0;

    // ---- STATE ----
    uint32_t orders = 0;
    uint32_t fills = 0;
    double latency_ms = 0.0;
    double spread_ticks = 0.0;
    bool disabled = false;

    void on_order() { orders++; }
    void on_fill()  { fills++; }

    void update_latency(double ms) { latency_ms = ms; }
    void update_spread(double ticks) { spread_ticks = ticks; }

    bool evaluate() {
        double fill_ratio = orders > 0 ? (double)fills / orders : 1.0;

        disabled =
            latency_ms > max_latency_ms ||
            fill_ratio < min_fill_ratio ||
            spread_ticks > max_spread_ticks;

        return disabled;
    }

    void reset_window() {
        orders = 0;
        fills = 0;
    }
};

} // namespace shadow
