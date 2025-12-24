// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/VenueHealth.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Single authoritative health snapshot of Binance venue
// OWNER: Jo
// LAST VERIFIED: 2024-12-22
//
// DESIGN:
// - Lock-free atomic state
// - Updated by feed + execution threads
// - Read atomically by strategies, risk, kill-switch
// - The ONLY shared cross-engine venue state
//
// TRACKED STATE:
// - WebSocket connection alive/dead
// - REST API alive/dead
// - Last heartbeat timestamps
// - Order reject count
// - Staleness detection
//
// HOT PATH GUARANTEES:
// - No allocation
// - No locks
// - No syscalls
// - All reads are acquire, all writes are release
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstdint>

namespace Chimera {
namespace Binance {

class VenueHealth {
public:
    VenueHealth() noexcept
        : ws_alive_(false)
        , rest_alive_(false)
        , last_ws_ts_ns_(0)
        , last_rest_ts_ns_(0)
        , reject_count_(0)
        , latency_ns_(0)
        , messages_received_(0)
    {}

    // ═══════════════════════════════════════════════════════════════════════
    // FEED UPDATES (called by connection threads)
    // ═══════════════════════════════════════════════════════════════════════
    
    inline void mark_ws_alive(uint64_t ts_ns) noexcept {
        ws_alive_.store(true, std::memory_order_release);
        last_ws_ts_ns_.store(ts_ns, std::memory_order_release);
        messages_received_.fetch_add(1, std::memory_order_relaxed);
    }

    inline void mark_ws_dead() noexcept {
        ws_alive_.store(false, std::memory_order_release);
    }

    inline void mark_rest_alive(uint64_t ts_ns) noexcept {
        rest_alive_.store(true, std::memory_order_release);
        last_rest_ts_ns_.store(ts_ns, std::memory_order_release);
    }

    inline void mark_rest_dead() noexcept {
        rest_alive_.store(false, std::memory_order_release);
    }
    
    inline void update_latency(uint64_t latency_ns) noexcept {
        latency_ns_.store(latency_ns, std::memory_order_release);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // EXECUTION FEEDBACK (called by order sender)
    // ═══════════════════════════════════════════════════════════════════════
    
    inline void record_reject() noexcept {
        reject_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    inline void reset_rejects() noexcept {
        reject_count_.store(0, std::memory_order_relaxed);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STATE READERS (hot path safe)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] inline bool ws_alive() const noexcept {
        return ws_alive_.load(std::memory_order_acquire);
    }

    [[nodiscard]] inline bool rest_alive() const noexcept {
        return rest_alive_.load(std::memory_order_acquire);
    }

    [[nodiscard]] inline uint64_t last_ws_ts() const noexcept {
        return last_ws_ts_ns_.load(std::memory_order_acquire);
    }

    [[nodiscard]] inline uint64_t last_rest_ts() const noexcept {
        return last_rest_ts_ns_.load(std::memory_order_acquire);
    }

    [[nodiscard]] inline uint64_t reject_count() const noexcept {
        return reject_count_.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] inline uint64_t latency_ns() const noexcept {
        return latency_ns_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] inline uint64_t messages_received() const noexcept {
        return messages_received_.load(std::memory_order_relaxed);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // HEALTH CHECKS
    // ═══════════════════════════════════════════════════════════════════════
    
    // Check if venue is healthy (WS alive and not stale)
    [[nodiscard]] inline bool healthy(uint64_t now_ns, uint64_t max_staleness_ns) const noexcept {
        if (!ws_alive()) return false;
        return (now_ns - last_ws_ts()) < max_staleness_ns;
    }
    
    // Check if we should stop trading (too many rejects)
    [[nodiscard]] inline bool too_many_rejects(uint64_t threshold) const noexcept {
        return reject_count() >= threshold;
    }
    
    // Check if latency is acceptable
    [[nodiscard]] inline bool latency_ok(uint64_t max_latency_ns) const noexcept {
        return latency_ns() < max_latency_ns;
    }
    
    // Combined health check for trading
    [[nodiscard]] inline bool can_trade(uint64_t now_ns, 
                                         uint64_t max_staleness_ns = 5000000000ULL,  // 5 seconds
                                         uint64_t max_latency_ns = 500000000ULL,     // 500ms
                                         uint64_t max_rejects = 10) const noexcept {
        return healthy(now_ns, max_staleness_ns) 
            && latency_ok(max_latency_ns)
            && !too_many_rejects(max_rejects);
    }

private:
    // Connection state - cache-line aligned to prevent false sharing
    alignas(64) std::atomic<bool>     ws_alive_;
    alignas(64) std::atomic<bool>     rest_alive_;
    
    // Timestamps
    alignas(64) std::atomic<uint64_t> last_ws_ts_ns_;
    std::atomic<uint64_t> last_rest_ts_ns_;
    
    // Execution feedback
    alignas(64) std::atomic<uint64_t> reject_count_;
    std::atomic<uint64_t> latency_ns_;
    std::atomic<uint64_t> messages_received_;
};

} // namespace Binance
} // namespace Chimera
