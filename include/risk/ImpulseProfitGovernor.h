#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

struct ImpulseProfitGovernor {
    /* ===== THREE-TIER ENTRY MODEL ===== */
    const double IMPULSE_STRONG      = 0.35;
    const double IMPULSE_DRIFT       = 0.04;
    const double IMPULSE_MICRO_DRIFT = 0.015;

    /* ===== SIZE MULTIPLIERS ===== */
    const double SIZE_FULL  = 1.00;
    const double SIZE_DRIFT = 0.50;
    const double SIZE_MICRO = 0.35;

    /* ===== DYNAMIC MAX LEGS ===== */
    const int BASE_MAX_LEGS   = 3;
    const int STRONG_MAX_LEGS = 5;
    const int ABS_MAX_LEGS    = 6;

    /* ===== STOP MANAGEMENT ===== */
    const double HARD_STOP_XAU    = 2.20;
    const double TRAIL_ENABLE_XAU = 1.40;
    const double TRAIL_OFFSET_XAU = 0.85;
    const double ATR_MICRO_XAU    = 0.32;

    /* ===== TIME WINDOWS ===== */
    const uint64_t ENTRY_FREEZE_NS = 250'000'000;

    /* ===== STATE ===== */
    uint64_t entry_freeze_until = 0;
    bool trailing_enabled = false;
    double stop_price = 0.0;

    /* ===== DYNAMIC MAX LEGS CALCULATION ===== */
    int allowed_legs(double impulse, bool latency_fast) const {
        if (!latency_fast) {
            return BASE_MAX_LEGS;
        }

        double aimp = std::abs(impulse);

        if (aimp >= IMPULSE_STRONG) {
            return STRONG_MAX_LEGS;
        }

        return BASE_MAX_LEGS;
    }

    /* ===== ENTRY GATE ===== */
    bool allow_entry(
        double impulse,
        double velocity,
        int current_legs,
        bool latency_fast,
        uint64_t now_ns,
        bool& is_drift,
        double& size_mult
    ) {
        is_drift = false;
        size_mult = SIZE_FULL;

        // Absolute safety ceiling (never bypass)
        if (current_legs >= ABS_MAX_LEGS) {
            return false;
        }

        // Dynamic max legs check
        int max_legs = allowed_legs(impulse, latency_fast);
        if (current_legs >= max_legs) {
            return false;
        }

        // Weak-signal freeze
        if (now_ns < entry_freeze_until) {
            return false;
        }

        const double aimp = std::abs(impulse);

        // TIER 2: Full impulse
        if (aimp >= IMPULSE_STRONG) {
            size_mult = SIZE_FULL;
            return true;
        }

        // TIER 1: Drift
        if (aimp >= IMPULSE_DRIFT &&
            ((impulse > 0 && velocity > 0) || (impulse < 0 && velocity < 0))) {
            is_drift = true;
            size_mult = SIZE_DRIFT;
            return true;
        }

        // TIER 0: Micro drift
        if (aimp >= IMPULSE_MICRO_DRIFT &&
            ((impulse > 0 && velocity > 0) || (impulse < 0 && velocity < 0))) {
            is_drift = true;
            size_mult = SIZE_MICRO;
            return true;
        }

        // Weak → freeze
        entry_freeze_until = now_ns + ENTRY_FREEZE_NS;
        return false;
    }

    void init_stop(double entry_price, bool is_long) {
        trailing_enabled = false;
        stop_price = is_long ? entry_price - HARD_STOP_XAU : entry_price + HARD_STOP_XAU;
    }

    void maybe_enable_trailing(double favorable_move) {
        if (!trailing_enabled && favorable_move >= TRAIL_ENABLE_XAU) {
            trailing_enabled = true;
        }
    }

    void update_stop(double price, double adverse_move, bool is_long) {
        if (!trailing_enabled || adverse_move < ATR_MICRO_XAU) return;
        
        double candidate = is_long ? price - TRAIL_OFFSET_XAU : price + TRAIL_OFFSET_XAU;
        
        if (is_long) stop_price = std::max(stop_price, candidate);
        else stop_price = std::min(stop_price, candidate);
    }

    void on_exit(uint64_t now_ns) {
        trailing_enabled = false;
    }
};
