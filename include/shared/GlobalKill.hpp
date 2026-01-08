// ═══════════════════════════════════════════════════════════════════════════════
// include/core/GlobalKill.hpp - IMMUTABLE CONTRACT
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Atomic emergency kill switch shared across all engines
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
//
// USAGE:
//   - Main thread sets kill on SIGINT/SIGTERM
//   - All symbol threads check kill on every tick
//   - Once set, never cleared (restart required)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstdint>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// GlobalKill - Single atomic kill switch
// ─────────────────────────────────────────────────────────────────────────────
// Cache-line aligned to prevent false sharing with other atomics.
// ─────────────────────────────────────────────────────────────────────────────
class GlobalKill {
public:
    GlobalKill() noexcept : killed_(false), kill_ts_ns_(0) {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // HOT PATH - Called on every tick by every thread
    // ═══════════════════════════════════════════════════════════════════════
    
    // Check if killed (relaxed read - fastest possible)
    [[nodiscard]] inline bool killed() const noexcept {
        return killed_.load(std::memory_order_relaxed);
    }
    
    // Operator bool for convenient if-checks
    [[nodiscard]] inline explicit operator bool() const noexcept {
        return killed();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // COLD PATH - Called once by main thread on signal
    // ═══════════════════════════════════════════════════════════════════════
    
    // Trigger kill (release semantics so all threads see it)
    inline void kill(uint64_t ts_ns = 0) noexcept {
        kill_ts_ns_.store(ts_ns, std::memory_order_relaxed);
        killed_.store(true, std::memory_order_release);
    }
    
    // Get kill timestamp (for logging)
    [[nodiscard]] inline uint64_t kill_timestamp() const noexcept {
        return kill_ts_ns_.load(std::memory_order_relaxed);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // FORBIDDEN - No reset capability
    // ═══════════════════════════════════════════════════════════════════════
    // Once killed, system must restart. This prevents accidental resume
    // after a kill condition that may have left state inconsistent.
    
    GlobalKill(const GlobalKill&) = delete;
    GlobalKill& operator=(const GlobalKill&) = delete;
    GlobalKill(GlobalKill&&) = delete;
    GlobalKill& operator=(GlobalKill&&) = delete;

private:
    alignas(64) std::atomic<bool>     killed_;
    alignas(64) std::atomic<uint64_t> kill_ts_ns_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global instance (extern declared, defined in one .cpp)
// ─────────────────────────────────────────────────────────────────────────────
// Usage:
//   In one .cpp file:  Chimera::GlobalKill g_kill;
//   In headers:        extern Chimera::GlobalKill g_kill;
//   In hot path:       if (g_kill.killed()) return;
// ─────────────────────────────────────────────────────────────────────────────

} // namespace Chimera
