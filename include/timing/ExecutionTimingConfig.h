#pragma once
#include <cstdint>

namespace timing {

// =====================
// EMPIRICAL PERCENTILES (from tail RTT analysis)
// =====================
static constexpr int NET_RTT_P50_MS  = 2;   // Median - half your trades
static constexpr int NET_RTT_P90_MS  = 5;   // 90% safe zone
static constexpr int NET_RTT_P95_MS  = 8;   // 95% safe zone (XAU cutoff)
static constexpr int NET_RTT_P99_MS  = 18;  // 99% - rare but dangerous
static constexpr int NET_RTT_MAX_MS  = 25;  // Observed maximum

static constexpr int RTT_SAMPLE_WINDOW     = 20;
static constexpr int RTT_RISING_WINDOW     = 3;   // For microbursts detection
static constexpr int RTT_TAIL_MEMORY_MS    = 500; // Block after tail event

static constexpr int GLOBAL_BLOCK_TIME_MS  = 30000;

// =====================
// XAUUSD — STAY INSIDE P95 (95% SAFE ZONE ONLY)
// =====================
static constexpr int XAU_ENTRY_RTT_MAX_MS    = 8;     // p95 hard gate - NO ENTRIES ABOVE THIS
static constexpr int XAU_CAUTION_RTT_MS      = 6;     // p90-p95 transition zone
static constexpr int XAU_BLOCK_RTT_MS        = 10;    // Safety margin above p95
static constexpr int XAU_MAX_OPEN_LEGS       = 2;     // Single position only
static constexpr int XAU_MAX_TRADES_PER_HOUR = 20;    // Quality over quantity
static constexpr int XAU_COOLDOWN_MS         = 800;  // Spacing between trades
static constexpr int XAU_BLOCK_ON_LOSS_COUNT = 2;     // ONE bad exit = pause

// =====================
// XAGUSD — CAN TOLERATE P99 (deeper, less jumpy)
// =====================
static constexpr int XAG_ENTRY_RTT_MAX_MS    = 12;    // More tolerance
static constexpr int XAG_BLOCK_RTT_MS        = 18;    // p99 threshold
static constexpr int XAG_MAX_OPEN_LEGS       = 2;
static constexpr int XAG_MAX_TRADES_PER_HOUR = 30;
static constexpr int XAG_COOLDOWN_MS         = 800;

// =====================
// EXIT SAFETY (SACRED - NEVER BLOCK)
// =====================
static constexpr int EXIT_RTT_MAX_MS = 25;  // Always allow exits up to this

}
