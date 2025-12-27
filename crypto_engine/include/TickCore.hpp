// ═══════════════════════════════════════════════════════════════════════════════
// include/core/TickCore.hpp - IMMUTABLE CONTRACT
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Canonical 64-byte tick structure for hot path
// OWNER: Jo
// LAST VERIFIED: 2024-12-27
//
// v3.12: CROSSED flag only set when bid > ask (true crossed book)
//        bid == ask (zero spread) is valid on fast-moving BTC
//
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
//
// DESIGN PRINCIPLES:
// - Exactly 64 bytes (one cache line)
// - No heap allocation
// - All fields needed for strategy decisions
// - Precomputed derived values (mid, imbalance)
// - Aligned to cache line boundary
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include "core/Venue.hpp"

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// TickCore - The canonical tick structure
// ─────────────────────────────────────────────────────────────────────────────
// This is the ONLY tick type that flows through the hot path.
// All venue-specific parsing must convert to this format.
//
// Layout (64 bytes total):
//   Bytes 0-7:   Identity (symbol_id, venue, flags, seq)
//   Bytes 8-47:  Prices and quantities (5 doubles)
//   Bytes 48-55: Timing (local_ts_ns)
//   Bytes 56-63: Precomputed (imbalance)
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(64) TickCore {
    // ═══════════════════════════════════════════════════════════════════════
    // IDENTITY BLOCK (8 bytes)
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t symbol_id;     // Unified symbol ID (0-65535)
    Venue    venue;         // Source venue (1 byte)
    uint8_t  flags;         // TickFlags bits (1 byte)
    uint32_t seq;           // Sequence number for gap detection (4 bytes)
    
    // ═══════════════════════════════════════════════════════════════════════
    // PRICE BLOCK (40 bytes)
    // ═══════════════════════════════════════════════════════════════════════
    double   bid;           // Best bid price
    double   ask;           // Best ask price
    double   bid_qty;       // Quantity at best bid
    double   ask_qty;       // Quantity at best ask
    double   mid;           // Precomputed: (bid + ask) / 2
    
    // ═══════════════════════════════════════════════════════════════════════
    // TIMING BLOCK (8 bytes)
    // ═══════════════════════════════════════════════════════════════════════
    uint64_t local_ts_ns;   // Local receive timestamp (nanoseconds)
    
    // ═══════════════════════════════════════════════════════════════════════
    // PRECOMPUTED BLOCK (8 bytes)
    // ═══════════════════════════════════════════════════════════════════════
    double   imbalance;     // Precomputed: (bid_qty - ask_qty) / (bid_qty + ask_qty)
    
    // ═══════════════════════════════════════════════════════════════════════
    // HOT PATH HELPERS (all inline, no allocation)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Spread in price units
    [[nodiscard]] inline double spread() const noexcept {
        return ask - bid;
    }
    
    // Spread in basis points
    [[nodiscard]] inline double spread_bps() const noexcept {
        if (mid <= 0.0) [[unlikely]] return 0.0;
        return (ask - bid) / mid * 10000.0;
    }
    
    // Is the book valid (not crossed)?
    // v3.11 FIX: Changed ask > bid to ask >= bid
    // BTC can have bid == ask (zero spread) during fast moves - still valid!
    [[nodiscard]] inline bool valid() const noexcept {
        return bid > 0.0 && ask > 0.0 && ask >= bid;
    }
    
    // Is this tick stale?
    [[nodiscard]] inline bool stale() const noexcept {
        return flags & TickFlags::STALE;
    }
    
    // Was there a sequence gap?
    [[nodiscard]] inline bool gapped() const noexcept {
        return flags & TickFlags::GAPPED;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // FACTORY (cold path - used by parsers)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Create from raw book data, precomputing derived fields
    [[nodiscard]] static inline TickCore make(
        uint16_t sym_id,
        Venue    v,
        uint32_t sequence,
        double   bid_price,
        double   ask_price,
        double   bid_quantity,
        double   ask_quantity,
        uint64_t ts_ns,
        uint8_t  tick_flags = TickFlags::NONE
    ) noexcept {
        TickCore t;
        t.symbol_id = sym_id;
        t.venue     = v;
        t.flags     = tick_flags;
        t.seq       = sequence;
        t.bid       = bid_price;
        t.ask       = ask_price;
        t.bid_qty   = bid_quantity;
        t.ask_qty   = ask_quantity;
        t.local_ts_ns = ts_ns;
        
        // Precompute derived values
        t.mid = (bid_price + ask_price) * 0.5;
        
        const double total_qty = bid_quantity + ask_quantity;
        t.imbalance = (total_qty > 0.0) 
            ? (bid_quantity - ask_quantity) / total_qty 
            : 0.0;
        
        // Set crossed flag if truly invalid (bid > ask means crossed book)
        // v3.12: bid == ask (zero spread) is VALID, not crossed
        if (bid_price > ask_price) {
            t.flags |= TickFlags::CROSSED;
        }
        
        return t;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// COMPILE-TIME VERIFICATION
// ═══════════════════════════════════════════════════════════════════════════════
static_assert(sizeof(TickCore) == 64, 
    "TickCore must be exactly 64 bytes (one cache line)");

static_assert(alignof(TickCore) == 64, 
    "TickCore must be aligned to 64-byte cache line boundary");

static_assert(offsetof(TickCore, symbol_id) == 0,
    "symbol_id must be at offset 0");

static_assert(offsetof(TickCore, bid) == 8,
    "bid must be at offset 8 (after identity block)");

static_assert(offsetof(TickCore, local_ts_ns) == 48,
    "local_ts_ns must be at offset 48");

static_assert(offsetof(TickCore, imbalance) == 56,
    "imbalance must be at offset 56");

} // namespace Chimera
