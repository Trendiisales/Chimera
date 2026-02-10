#pragma once
#include <vector>
#include <cmath>
#include <cstdint>

namespace shadow {

enum class PxExitReason {
    NONE,
    PARTIAL_TAKE,
    RUNNER_STOP,
    TIME_STOP,
    HARD_STOP
};

struct PartialExitRunner {
    // ---- CONFIG ----
    double r_partial1 = 0.50;   // take 30%
    double r_partial2 = 1.00;   // take 40%
    double r_trail_start = 0.80;
    double r_trail_step = 0.25;
    double runner_pct = 0.30;
    uint32_t time_stop_ticks = 600;

    // ---- STATE ----
    double entry = 0.0;
    double stop = 0.0;
    double best = 0.0;
    double trail_stop = 0.0;
    double size = 0.0;
    double runner_size = 0.0;
    uint32_t ticks = 0;
    int dir = 0;
    bool active = false;
    bool p1_done = false;
    bool p2_done = false;

    void on_entry(double entry_px, double stop_px, double total_size, int direction) {
        entry = entry_px;
        stop = stop_px;
        best = entry_px;
        trail_stop = stop_px;
        size = total_size;
        runner_size = total_size * runner_pct;
        ticks = 0;
        dir = direction;
        active = true;
        p1_done = false;
        p2_done = false;
    }

    PxExitReason on_tick(double px, double& qty_to_exit) {
        qty_to_exit = 0.0;
        if (!active) return PxExitReason::NONE;

        ticks++;

        // hard stop
        if ((dir == 1 && px <= stop) || (dir == -1 && px >= stop)) {
            qty_to_exit = size;
            active = false;
            return PxExitReason::HARD_STOP;
        }

        update_best(px);
        double R = std::fabs(entry - stop);
        double move = dir == 1 ? (best - entry) : (entry - best);

        // partial 1
        if (!p1_done && move >= r_partial1 * R) {
            qty_to_exit = size * 0.30;
            size -= qty_to_exit;
            p1_done = true;
            return PxExitReason::PARTIAL_TAKE;
        }

        // partial 2
        if (!p2_done && move >= r_partial2 * R) {
            qty_to_exit = size * 0.40;
            size -= qty_to_exit;
            p2_done = true;
            return PxExitReason::PARTIAL_TAKE;
        }

        // trailing runner
        if (move >= r_trail_start * R) {
            double new_trail =
                dir == 1 ? best - r_trail_step * R
                         : best + r_trail_step * R;

            if ((dir == 1 && new_trail > trail_stop) ||
                (dir == -1 && new_trail < trail_stop)) {
                trail_stop = new_trail;
            }
        }

        if ((dir == 1 && px <= trail_stop) ||
            (dir == -1 && px >= trail_stop)) {
            qty_to_exit = size;
            active = false;
            return PxExitReason::RUNNER_STOP;
        }

        if (ticks >= time_stop_ticks) {
            qty_to_exit = size;
            active = false;
            return PxExitReason::TIME_STOP;
        }

        return PxExitReason::NONE;
    }

    void reset() {
        active = false;
        p1_done = false;
        p2_done = false;
        ticks = 0;
    }

private:
    void update_best(double px) {
        if ((dir == 1 && px > best) ||
            (dir == -1 && px < best)) {
            best = px;
        }
    }
};

// Symbol-specific profiles (from Document 7)
struct ExitProfile {
    double r_p1;
    double r_p2;
    double r_trail;
    double r_step;
    uint32_t time_stop;
};

inline ExitProfile getXauProfile() {
    return {0.50, 1.00, 0.80, 0.25, 600};
}

inline ExitProfile getXagProfile() {
    return {0.60, 1.20, 0.90, 0.30, 720};
}

inline ExitProfile getNasProfile() {
    return {0.70, 1.40, 1.10, 0.35, 420};
}

inline ExitProfile getUs30Profile() {
    return {0.80, 1.60, 1.30, 0.40, 360};
}

} // namespace shadow
