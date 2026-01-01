// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/config/SymbolProfiles.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Symbol-specific trading profiles with edge quality ranking
// OWNER: Jo
// VERSION: v3.0
//
// EDGE QUALITY RANKING:
// TIER 1 (CORE EDGE): BTCUSDT, ETHUSDT, SOLUSDT - Trade these first
// TIER 2 (CONDITIONAL): XAUUSD, NAS100, SPX500 - Session filtered only
// TIER 3 (SENSOR): XAGUSD, EURUSD, USDJPY - Limited capital
// TIER 4 (DISABLED): US30, GBPUSD, AUDUSD, AUDNZD, USDCAD, USDCHF
//
// RULE: More symbols ≠ more edge. 90% of profits from 2-4 instruments.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <iostream>
#include <iomanip>

namespace Chimera {
namespace Config {

// ─────────────────────────────────────────────────────────────────────────────
// Edge Quality Tier
// ─────────────────────────────────────────────────────────────────────────────
enum class EdgeTier : uint8_t {
    TIER_1_CORE     = 1,   // Primary money engines - trade first
    TIER_2_COND     = 2,   // Strong but session-filtered
    TIER_3_SENSOR   = 3,   // Limited capital / regime sensing
    TIER_4_DISABLED = 4    // Do not scale, sensor only
};

inline const char* tier_str(EdgeTier t) noexcept {
    switch (t) {
        case EdgeTier::TIER_1_CORE:     return "TIER_1_CORE";
        case EdgeTier::TIER_2_COND:     return "TIER_2_CONDITIONAL";
        case EdgeTier::TIER_3_SENSOR:   return "TIER_3_SENSOR";
        case EdgeTier::TIER_4_DISABLED: return "TIER_4_DISABLED";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution Mode
// ─────────────────────────────────────────────────────────────────────────────
enum class SymbolExecMode : uint8_t {
    TAKER_ONLY,    // Always take liquidity
    MAKER_ONLY,    // Always post passive
    HYBRID         // Try maker, fallback to taker
};

inline const char* exec_mode_str(SymbolExecMode m) noexcept {
    switch (m) {
        case SymbolExecMode::TAKER_ONLY: return "TAKER";
        case SymbolExecMode::MAKER_ONLY: return "MAKER";
        case SymbolExecMode::HYBRID:     return "HYBRID";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Filter
// ─────────────────────────────────────────────────────────────────────────────
enum class SessionFilter : uint8_t {
    ALL_SESSIONS,
    LONDON_NY_ONLY,
    NY_OPEN_ONLY,
    ASIA_ONLY,
    LONDON_ONLY
};

inline const char* session_str(SessionFilter s) noexcept {
    switch (s) {
        case SessionFilter::ALL_SESSIONS:   return "ALL";
        case SessionFilter::LONDON_NY_ONLY: return "LONDON_NY";
        case SessionFilter::NY_OPEN_ONLY:   return "NY_OPEN";
        case SessionFilter::ASIA_ONLY:      return "ASIA";
        case SessionFilter::LONDON_ONLY:    return "LONDON";
        default: return "UNKNOWN";
    }
}

// Check if current UTC hour is within session
inline bool is_session_active(SessionFilter filter, int utc_hour) noexcept {
    switch (filter) {
        case SessionFilter::ALL_SESSIONS:
            return true;
        case SessionFilter::LONDON_NY_ONLY:
            // London: 08-16, NY: 13-21 → Overlap: 13-16
            return (utc_hour >= 8 && utc_hour <= 21);
        case SessionFilter::NY_OPEN_ONLY:
            // NY Open: 13:30-16:00 UTC → 13-16
            return (utc_hour >= 13 && utc_hour <= 16);
        case SessionFilter::ASIA_ONLY:
            // Asia: 00-08 UTC
            return (utc_hour >= 0 && utc_hour <= 8);
        case SessionFilter::LONDON_ONLY:
            // London: 08-16 UTC
            return (utc_hour >= 8 && utc_hour <= 16);
        default:
            return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Class
// ─────────────────────────────────────────────────────────────────────────────
enum class AssetClass : uint8_t {
    CRYPTO,
    FOREX,
    METALS,
    INDICES
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Profile - Complete Configuration
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolProfile {
    // Identity
    const char* symbol;
    uint16_t symbol_id;
    AssetClass asset_class;
    EdgeTier tier;
    
    // Enabled/Mode
    bool enabled;
    bool live_enabled;           // Can go live (vs shadow only)
    SymbolExecMode exec_mode;
    SessionFilter session;
    
    // Size (shadow ignores, live uses)
    double base_size_usd;        // For crypto
    double base_size_lots;       // For CFD
    int max_position;
    
    // Spread Gates (bps for crypto, points for CFD)
    double min_spread;
    double max_spread;
    
    // Confidence Thresholds
    double entry_confidence_min;
    double exit_confidence_min;
    
    // Time Limits
    uint64_t max_hold_ms;
    uint64_t min_hold_ms;
    
    // Exit Targets (bps for crypto, points for CFD)
    double take_profit;
    double stop_loss;
    
    // Expectancy Guards
    double expectancy_min_bps;
    double expectancy_slope_min;
    
    // Maker Settings
    uint64_t maker_timeout_ms;
    double taker_slippage_cap_bps;
    
    // Latency Guards
    uint64_t latency_guard_us;
    int queue_position_max;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Helper Methods
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] bool can_trade(int utc_hour) const noexcept {
        if (!enabled) return false;
        if (tier == EdgeTier::TIER_4_DISABLED) return false;
        return is_session_active(session, utc_hour);
    }
    
    [[nodiscard]] bool can_go_live() const noexcept {
        return enabled && live_enabled && tier != EdgeTier::TIER_4_DISABLED;
    }
    
    [[nodiscard]] double get_size_multiplier() const noexcept {
        switch (tier) {
            case EdgeTier::TIER_1_CORE:     return 1.0;
            case EdgeTier::TIER_2_COND:     return 0.75;
            case EdgeTier::TIER_3_SENSOR:   return 0.25;
            case EdgeTier::TIER_4_DISABLED: return 0.0;
            default: return 0.0;
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// DEFAULT PROFILES - Based on Edge Quality Analysis
// ═══════════════════════════════════════════════════════════════════════════════

namespace Profiles {

// ─────────────────────────────────────────────────────────────────────────────
// TIER 1: CORE EDGE (Trade These First)
// ─────────────────────────────────────────────────────────────────────────────

constexpr SymbolProfile BTCUSDT = {
    .symbol = "BTCUSDT",
    .symbol_id = 0,
    .asset_class = AssetClass::CRYPTO,
    .tier = EdgeTier::TIER_1_CORE,
    
    .enabled = true,
    .live_enabled = true,
    .exec_mode = SymbolExecMode::HYBRID,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 25.0,
    .base_size_lots = 0.0,
    .max_position = 1,
    
    .min_spread = 0.4,
    .max_spread = 2.0,
    
    .entry_confidence_min = 0.75,
    .exit_confidence_min = 0.55,
    
    .max_hold_ms = 2200,
    .min_hold_ms = 120,
    
    .take_profit = 1.2,
    .stop_loss = 1.8,
    
    .expectancy_min_bps = 0.4,
    .expectancy_slope_min = 0.002,
    
    .maker_timeout_ms = 180,
    .taker_slippage_cap_bps = 0.8,
    
    .latency_guard_us = 900,
    .queue_position_max = 6
};

constexpr SymbolProfile ETHUSDT = {
    .symbol = "ETHUSDT",
    .symbol_id = 1,
    .asset_class = AssetClass::CRYPTO,
    .tier = EdgeTier::TIER_1_CORE,
    
    .enabled = true,
    .live_enabled = true,
    .exec_mode = SymbolExecMode::HYBRID,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 20.0,
    .base_size_lots = 0.0,
    .max_position = 1,
    
    .min_spread = 0.5,
    .max_spread = 2.5,
    
    .entry_confidence_min = 0.78,
    .exit_confidence_min = 0.58,
    
    .max_hold_ms = 2000,
    .min_hold_ms = 120,
    
    .take_profit = 1.4,
    .stop_loss = 2.0,
    
    .expectancy_min_bps = 0.45,
    .expectancy_slope_min = 0.0025,
    
    .maker_timeout_ms = 160,
    .taker_slippage_cap_bps = 1.0,
    
    .latency_guard_us = 900,
    .queue_position_max = 5
};

constexpr SymbolProfile SOLUSDT = {
    .symbol = "SOLUSDT",
    .symbol_id = 2,
    .asset_class = AssetClass::CRYPTO,
    .tier = EdgeTier::TIER_1_CORE,
    
    .enabled = true,
    .live_enabled = true,
    .exec_mode = SymbolExecMode::TAKER_ONLY,  // Maker lies more on SOL
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 15.0,
    .base_size_lots = 0.0,
    .max_position = 1,
    
    .min_spread = 0.8,
    .max_spread = 3.0,
    
    .entry_confidence_min = 0.82,
    .exit_confidence_min = 0.60,
    
    .max_hold_ms = 1600,
    .min_hold_ms = 100,
    
    .take_profit = 1.8,
    .stop_loss = 2.5,
    
    .expectancy_min_bps = 0.6,
    .expectancy_slope_min = 0.003,
    
    .maker_timeout_ms = 0,  // N/A - taker only
    .taker_slippage_cap_bps = 1.2,
    
    .latency_guard_us = 800,
    .queue_position_max = 0
};

// ─────────────────────────────────────────────────────────────────────────────
// TIER 2: CONDITIONAL (Session Filtered)
// ─────────────────────────────────────────────────────────────────────────────

constexpr SymbolProfile XAUUSD = {
    .symbol = "XAUUSD",
    .symbol_id = 10,
    .asset_class = AssetClass::METALS,
    .tier = EdgeTier::TIER_2_COND,
    
    .enabled = true,
    .live_enabled = true,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::LONDON_NY_ONLY,  // CRITICAL
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.01,
    .max_position = 1,
    
    .min_spread = 8.0,    // points
    .max_spread = 25.0,   // points - hard gate
    
    .entry_confidence_min = 0.80,
    .exit_confidence_min = 0.60,
    
    .max_hold_ms = 3000,
    .min_hold_ms = 200,
    
    .take_profit = 35.0,  // points
    .stop_loss = 55.0,    // points
    
    .expectancy_min_bps = 0.7,
    .expectancy_slope_min = 0.003,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 1.5,
    
    .latency_guard_us = 1200,
    .queue_position_max = 0
};

constexpr SymbolProfile NAS100 = {
    .symbol = "NAS100",
    .symbol_id = 20,
    .asset_class = AssetClass::INDICES,
    .tier = EdgeTier::TIER_2_COND,
    
    .enabled = true,
    .live_enabled = true,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::NY_OPEN_ONLY,  // CRITICAL
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.01,
    .max_position = 1,
    
    .min_spread = 5.0,
    .max_spread = 20.0,
    
    .entry_confidence_min = 0.82,
    .exit_confidence_min = 0.62,
    
    .max_hold_ms = 1800,
    .min_hold_ms = 150,
    
    .take_profit = 25.0,
    .stop_loss = 40.0,
    
    .expectancy_min_bps = 0.6,
    .expectancy_slope_min = 0.003,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 1.8,
    
    .latency_guard_us = 1100,
    .queue_position_max = 0
};

constexpr SymbolProfile SPX500 = {
    .symbol = "SPX500",
    .symbol_id = 21,
    .asset_class = AssetClass::INDICES,
    .tier = EdgeTier::TIER_2_COND,
    
    .enabled = true,
    .live_enabled = true,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::NY_OPEN_ONLY,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.01,
    .max_position = 1,
    
    .min_spread = 6.0,
    .max_spread = 22.0,
    
    .entry_confidence_min = 0.82,
    .exit_confidence_min = 0.62,
    
    .max_hold_ms = 2000,
    .min_hold_ms = 150,
    
    .take_profit = 22.0,
    .stop_loss = 38.0,
    
    .expectancy_min_bps = 0.55,
    .expectancy_slope_min = 0.003,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 1.8,
    
    .latency_guard_us = 1100,
    .queue_position_max = 0
};

// ─────────────────────────────────────────────────────────────────────────────
// TIER 3: SENSOR (Limited Capital)
// ─────────────────────────────────────────────────────────────────────────────

constexpr SymbolProfile XAGUSD = {
    .symbol = "XAGUSD",
    .symbol_id = 11,
    .asset_class = AssetClass::METALS,
    .tier = EdgeTier::TIER_3_SENSOR,
    
    .enabled = true,
    .live_enabled = false,  // Shadow only
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::LONDON_NY_ONLY,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.005,  // Half size
    .max_position = 1,
    
    .min_spread = 10.0,
    .max_spread = 30.0,
    
    .entry_confidence_min = 0.85,
    .exit_confidence_min = 0.65,
    
    .max_hold_ms = 2500,
    .min_hold_ms = 200,
    
    .take_profit = 40.0,
    .stop_loss = 60.0,
    
    .expectancy_min_bps = 0.8,
    .expectancy_slope_min = 0.004,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 2.0,
    
    .latency_guard_us = 1500,
    .queue_position_max = 0
};

constexpr SymbolProfile EURUSD = {
    .symbol = "EURUSD",
    .symbol_id = 30,
    .asset_class = AssetClass::FOREX,
    .tier = EdgeTier::TIER_3_SENSOR,
    
    .enabled = true,
    .live_enabled = false,  // Shadow only
    .exec_mode = SymbolExecMode::HYBRID,
    .session = SessionFilter::LONDON_NY_ONLY,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.01,
    .max_position = 1,
    
    .min_spread = 0.1,
    .max_spread = 1.5,
    
    .entry_confidence_min = 0.88,
    .exit_confidence_min = 0.68,
    
    .max_hold_ms = 2500,
    .min_hold_ms = 150,
    
    .take_profit = 0.8,
    .stop_loss = 1.5,
    
    .expectancy_min_bps = 0.3,  // Very thin edge
    .expectancy_slope_min = 0.002,
    
    .maker_timeout_ms = 200,
    .taker_slippage_cap_bps = 0.5,
    
    .latency_guard_us = 1000,
    .queue_position_max = 4
};

constexpr SymbolProfile USDJPY = {
    .symbol = "USDJPY",
    .symbol_id = 31,
    .asset_class = AssetClass::FOREX,
    .tier = EdgeTier::TIER_3_SENSOR,
    
    .enabled = true,
    .live_enabled = false,  // Shadow only
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.01,
    .max_position = 1,
    
    .min_spread = 0.3,
    .max_spread = 2.0,
    
    .entry_confidence_min = 0.85,
    .exit_confidence_min = 0.65,
    
    .max_hold_ms = 2200,
    .min_hold_ms = 150,
    
    .take_profit = 1.0,
    .stop_loss = 1.8,
    
    .expectancy_min_bps = 0.4,
    .expectancy_slope_min = 0.003,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 1.0,
    
    .latency_guard_us = 1000,
    .queue_position_max = 0
};

// ─────────────────────────────────────────────────────────────────────────────
// TIER 4: DISABLED (Sensor Only - No Trading)
// ─────────────────────────────────────────────────────────────────────────────

constexpr SymbolProfile US30 = {
    .symbol = "US30",
    .symbol_id = 22,
    .asset_class = AssetClass::INDICES,
    .tier = EdgeTier::TIER_4_DISABLED,
    
    .enabled = true,     // For regime sensing
    .live_enabled = false,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.0,  // No trading
    .max_position = 0,
    
    .min_spread = 0.0,
    .max_spread = 999.0,
    
    .entry_confidence_min = 1.0,  // Never trades
    .exit_confidence_min = 1.0,
    
    .max_hold_ms = 0,
    .min_hold_ms = 0,
    
    .take_profit = 0.0,
    .stop_loss = 0.0,
    
    .expectancy_min_bps = 999.0,  // Impossible threshold
    .expectancy_slope_min = 1.0,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 0.0,
    
    .latency_guard_us = 0,
    .queue_position_max = 0
};

constexpr SymbolProfile GBPUSD = {
    .symbol = "GBPUSD",
    .symbol_id = 32,
    .asset_class = AssetClass::FOREX,
    .tier = EdgeTier::TIER_4_DISABLED,
    
    .enabled = true,
    .live_enabled = false,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.0,
    .max_position = 0,
    
    .min_spread = 0.0,
    .max_spread = 999.0,
    
    .entry_confidence_min = 1.0,
    .exit_confidence_min = 1.0,
    
    .max_hold_ms = 0,
    .min_hold_ms = 0,
    
    .take_profit = 0.0,
    .stop_loss = 0.0,
    
    .expectancy_min_bps = 999.0,
    .expectancy_slope_min = 1.0,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 0.0,
    
    .latency_guard_us = 0,
    .queue_position_max = 0
};

constexpr SymbolProfile AUDUSD = {
    .symbol = "AUDUSD",
    .symbol_id = 33,
    .asset_class = AssetClass::FOREX,
    .tier = EdgeTier::TIER_4_DISABLED,
    
    .enabled = true,
    .live_enabled = false,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.0,
    .max_position = 0,
    
    .min_spread = 0.0,
    .max_spread = 999.0,
    
    .entry_confidence_min = 1.0,
    .exit_confidence_min = 1.0,
    
    .max_hold_ms = 0,
    .min_hold_ms = 0,
    
    .take_profit = 0.0,
    .stop_loss = 0.0,
    
    .expectancy_min_bps = 999.0,
    .expectancy_slope_min = 1.0,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 0.0,
    
    .latency_guard_us = 0,
    .queue_position_max = 0
};

constexpr SymbolProfile USDCAD = {
    .symbol = "USDCAD",
    .symbol_id = 34,
    .asset_class = AssetClass::FOREX,
    .tier = EdgeTier::TIER_4_DISABLED,
    
    .enabled = true,
    .live_enabled = false,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.0,
    .max_position = 0,
    
    .min_spread = 0.0,
    .max_spread = 999.0,
    
    .entry_confidence_min = 1.0,
    .exit_confidence_min = 1.0,
    
    .max_hold_ms = 0,
    .min_hold_ms = 0,
    
    .take_profit = 0.0,
    .stop_loss = 0.0,
    
    .expectancy_min_bps = 999.0,
    .expectancy_slope_min = 1.0,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 0.0,
    
    .latency_guard_us = 0,
    .queue_position_max = 0
};

constexpr SymbolProfile USDCHF = {
    .symbol = "USDCHF",
    .symbol_id = 35,
    .asset_class = AssetClass::FOREX,
    .tier = EdgeTier::TIER_4_DISABLED,
    
    .enabled = true,
    .live_enabled = false,
    .exec_mode = SymbolExecMode::TAKER_ONLY,
    .session = SessionFilter::ALL_SESSIONS,
    
    .base_size_usd = 0.0,
    .base_size_lots = 0.0,
    .max_position = 0,
    
    .min_spread = 0.0,
    .max_spread = 999.0,
    
    .entry_confidence_min = 1.0,
    .exit_confidence_min = 1.0,
    
    .max_hold_ms = 0,
    .min_hold_ms = 0,
    
    .take_profit = 0.0,
    .stop_loss = 0.0,
    
    .expectancy_min_bps = 999.0,
    .expectancy_slope_min = 1.0,
    
    .maker_timeout_ms = 0,
    .taker_slippage_cap_bps = 0.0,
    
    .latency_guard_us = 0,
    .queue_position_max = 0
};

} // namespace Profiles

// ═══════════════════════════════════════════════════════════════════════════════
// Profile Manager
// ═══════════════════════════════════════════════════════════════════════════════

class ProfileManager {
public:
    ProfileManager() {
        // Initialize with default profiles
        profiles_["BTCUSDT"] = Profiles::BTCUSDT;
        profiles_["ETHUSDT"] = Profiles::ETHUSDT;
        profiles_["SOLUSDT"] = Profiles::SOLUSDT;
        profiles_["XAUUSD"]  = Profiles::XAUUSD;
        profiles_["NAS100"]  = Profiles::NAS100;
        profiles_["SPX500"]  = Profiles::SPX500;
        profiles_["XAGUSD"]  = Profiles::XAGUSD;
        profiles_["EURUSD"]  = Profiles::EURUSD;
        profiles_["USDJPY"]  = Profiles::USDJPY;
        profiles_["US30"]    = Profiles::US30;
        profiles_["GBPUSD"]  = Profiles::GBPUSD;
        profiles_["AUDUSD"]  = Profiles::AUDUSD;
        profiles_["USDCAD"]  = Profiles::USDCAD;
        profiles_["USDCHF"]  = Profiles::USDCHF;
    }
    
    [[nodiscard]] const SymbolProfile* get(const std::string& symbol) const noexcept {
        auto it = profiles_.find(symbol);
        if (it == profiles_.end()) return nullptr;
        return &it->second;
    }
    
    [[nodiscard]] const SymbolProfile* get_by_id(uint16_t id) const noexcept {
        for (const auto& [name, profile] : profiles_) {
            if (profile.symbol_id == id) return &profile;
        }
        return nullptr;
    }
    
    void set(const std::string& symbol, const SymbolProfile& profile) noexcept {
        profiles_[symbol] = profile;
    }
    
    void print_summary() const {
        std::cout << "\n╔══════════════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                    SYMBOL PROFILES v3.0                                  ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  Symbol   │ Tier            │ Mode   │ Session   │ Live │ Size          ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════════════╣\n";
        
        for (const auto& [name, p] : profiles_) {
            std::cout << "║  " << std::setw(8) << std::left << p.symbol
                     << " │ " << std::setw(15) << tier_str(p.tier)
                     << " │ " << std::setw(6) << exec_mode_str(p.exec_mode)
                     << " │ " << std::setw(9) << session_str(p.session)
                     << " │ " << (p.live_enabled ? "YES " : "NO  ")
                     << " │ ";
            
            if (p.asset_class == AssetClass::CRYPTO) {
                std::cout << "$" << std::setw(4) << std::fixed << std::setprecision(0) << p.base_size_usd;
            } else {
                std::cout << std::setw(4) << std::setprecision(3) << p.base_size_lots << "L";
            }
            std::cout << "         ║\n";
        }
        
        std::cout << "╚══════════════════════════════════════════════════════════════════════════╝\n\n";
    }
    
    // Get all tradeable symbols for current hour
    [[nodiscard]] std::vector<std::string> get_active_symbols(int utc_hour) const {
        std::vector<std::string> result;
        for (const auto& [name, profile] : profiles_) {
            if (profile.can_trade(utc_hour)) {
                result.push_back(name);
            }
        }
        return result;
    }
    
    // Get live-enabled symbols only
    [[nodiscard]] std::vector<std::string> get_live_symbols() const {
        std::vector<std::string> result;
        for (const auto& [name, profile] : profiles_) {
            if (profile.can_go_live()) {
                result.push_back(name);
            }
        }
        return result;
    }

private:
    std::unordered_map<std::string, SymbolProfile> profiles_;
};

// Global instance
inline ProfileManager& getProfileManager() {
    static ProfileManager instance;
    return instance;
}

} // namespace Config
} // namespace Chimera
