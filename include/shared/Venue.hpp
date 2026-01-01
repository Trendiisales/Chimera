// ═══════════════════════════════════════════════════════════════════════════════
// include/core/Venue.hpp - IMMUTABLE CONTRACT
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Fundamental venue and side enums used throughout the system
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Venue - Trading venue identifier
// ─────────────────────────────────────────────────────────────────────────────
// Used to identify the source of ticks and destination of orders.
// Each engine is dedicated to one venue.
// ─────────────────────────────────────────────────────────────────────────────
enum class Venue : uint8_t {
    UNKNOWN  = 0,   // Invalid/unset
    BINANCE  = 1,   // Binance cryptocurrency exchange
    CTRADER  = 2    // cTrader CFD/Forex via FIX
};

// String conversion (cold path only)
inline const char* to_string(Venue v) noexcept {
    switch (v) {
        case Venue::BINANCE: return "BINANCE";
        case Venue::CTRADER: return "CTRADER";
        default:             return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Side - Order/position side
// ─────────────────────────────────────────────────────────────────────────────
enum class Side : uint8_t {
    None = 0,   // No side (flat)
    Buy  = 1,   // Long / Buy
    Sell = 2    // Short / Sell
};

// String conversion (cold path only)
inline const char* to_string(Side s) noexcept {
    switch (s) {
        case Side::Buy:  return "BUY";
        case Side::Sell: return "SELL";
        default:         return "NONE";
    }
}

// Flip side (hot path safe)
[[nodiscard]] inline constexpr Side flip(Side s) noexcept {
    if (s == Side::Buy) return Side::Sell;
    if (s == Side::Sell) return Side::Buy;
    return Side::None;
}

// ─────────────────────────────────────────────────────────────────────────────
// TickFlags - Bit flags for tick metadata
// ─────────────────────────────────────────────────────────────────────────────
namespace TickFlags {
    constexpr uint8_t NONE      = 0x00;
    constexpr uint8_t STALE     = 0x01;  // Tick is older than threshold
    constexpr uint8_t SYNTHETIC = 0x02;  // Generated, not from exchange
    constexpr uint8_t GAPPED    = 0x04;  // Sequence gap detected
    constexpr uint8_t SNAPSHOT  = 0x08;  // From REST snapshot, not stream
    constexpr uint8_t CROSSED   = 0x10;  // Bid >= Ask (invalid book)
}

} // namespace Chimera
