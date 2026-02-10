#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include "ShadowTypes.hpp"  // Use ExitReason from here

namespace shadow {

struct RangeFailureExit {
    // --- CONFIG ---
    double extension_r_required;    // e.g. 0.60R
    double retrace_r_required;      // e.g. 0.35R
    uint32_t max_ticks_without_high; // e.g. 40
    uint32_t hard_hold_ticks;       // e.g. 10
    uint32_t time_stop_ticks;       // e.g. 300

    // --- STATE ---
    double entry_price = 0.0;
    double stop_price = 0.0;
    double best_price = 0.0;
    uint32_t ticks_since_entry = 0;
    uint32_t ticks_since_best = 0;
    bool in_position = false;
    int direction = 0; // +1 long, -1 short

    void on_entry(double entry, double stop, int dir) {
        entry_price = entry;
        stop_price = stop;
        best_price = entry;
        ticks_since_entry = 0;
        ticks_since_best = 0;
        in_position = true;
        direction = dir;
    }

    ExitReason on_tick(double price) {
        if (!in_position) return ExitReason::NONE;

        ticks_since_entry++;

        // --- STOP LOSS (always honored) ---
        if ((direction == 1 && price <= stop_price) ||
            (direction == -1 && price >= stop_price)) {
            return ExitReason::STOP_LOSS;
        }

        // --- HARD HOLD ---
        if (ticks_since_entry < hard_hold_ticks) {
            update_best(price);
            return ExitReason::NONE;
        }

        double R = std::fabs(entry_price - stop_price);
        double favorable_move =
            direction == 1 ? (best_price - entry_price)
                           : (entry_price - best_price);

        // --- TIME STOP ---
        if (ticks_since_entry >= time_stop_ticks &&
            favorable_move < 0.25 * R) {
            return ExitReason::TIME_STOP;
        }

        // --- RANGE FAILURE LOGIC ---
        if (favorable_move >= extension_r_required * R) {
            double retrace =
                direction == 1 ? (best_price - price)
                               : (price - best_price);

            if (retrace >= retrace_r_required * R &&
                ticks_since_best >= max_ticks_without_high) {
                return ExitReason::RANGE_FAILURE;
            }
        }

        update_best(price);
        return ExitReason::NONE;
    }

    void reset() {
        in_position = false;
        ticks_since_entry = 0;
        ticks_since_best = 0;
    }

private:
    void update_best(double price) {
        if ((direction == 1 && price > best_price) ||
            (direction == -1 && price < best_price)) {
            best_price = price;
            ticks_since_best = 0;
        } else {
            ticks_since_best++;
        }
    }
};

// Symbol-specific parameters (from Document 6)
inline RangeFailureExit getXauRangeExit() {
    return {
        0.60,  // extension_r_required
        0.35,  // retrace_r_required
        40,    // max_ticks_without_high
        12,    // hard_hold_ticks (Document: was 10, now 12 per exit logic spec)
        300    // time_stop_ticks
    };
}

inline RangeFailureExit getXagRangeExit() {
    return {
        0.75,  // extension_r_required
        0.40,  // retrace_r_required
        55,    // max_ticks_without_high
        12,    // hard_hold_ticks
        420    // time_stop_ticks
    };
}

inline RangeFailureExit getNasRangeExit() {
    return {
        0.80,  // extension_r_required
        0.30,  // retrace_r_required
        65,    // max_ticks_without_high
        8,     // hard_hold_ticks
        220    // time_stop_ticks
    };
}

inline RangeFailureExit getUs30RangeExit() {
    return {
        1.00,  // extension_r_required
        0.45,  // retrace_r_required
        80,    // max_ticks_without_high
        6,     // hard_hold_ticks
        180    // time_stop_ticks
    };
}

// Helper function to get config by symbol name
inline RangeFailureExit getRangeExitConfig(const std::string& symbol) {
    if (symbol == "XAUUSD") return getXauRangeExit();
    if (symbol == "XAGUSD") return getXagRangeExit();
    if (symbol == "NAS100") return getNasRangeExit();
    if (symbol == "US30") return getUs30RangeExit();
    return getXauRangeExit();  // default to XAU
}

} // namespace shadow
