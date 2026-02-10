#pragma once

enum class Metal { XAU, XAG };

struct MetalRegimeConfig {
    double max_fix_rtt_ms;
    double max_spread;
    double min_edge_after_costs;
    double size_multiplier;
    uint32_t min_cooldown_ms;
    uint32_t max_trades_per_hour;
};

static const MetalRegimeConfig XAU_REGIME = {
    .max_fix_rtt_ms = 25.0,
    .max_spread = 22.0,
    .min_edge_after_costs = 3.0,
    .size_multiplier = 1.0,
    .min_cooldown_ms = 2000,      // 2 seconds (was 30s)
    .max_trades_per_hour = 60     // 60/hour (was 12)
};

static const MetalRegimeConfig XAG_REGIME = {
    .max_fix_rtt_ms = 20.0,
    .max_spread = 18.0,
    .min_edge_after_costs = 4.5,
    .size_multiplier = 0.4,
    .min_cooldown_ms = 3000,      // 3 seconds (was 120s)
    .max_trades_per_hour = 30     // 30/hour (was 4)
};
