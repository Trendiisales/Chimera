// ═══════════════════════════════════════════════════════════════════════════════
// include/shared/DailyLossGuard.hpp - IMMUTABLE CONTRACT
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔒 LOCKED
// PURPOSE: Atomic daily PnL guard shared across BOTH engines
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DO NOT MODIFY WITHOUT EXPLICIT OWNER APPROVAL
//
// This is ONE OF ONLY TWO shared structures between engines:
//   1. GlobalKill - emergency stop
//   2. DailyLossGuard - combined PnL limit
//
// DESIGN:
//   - Both engines call on_fill() with realized PnL (in NZD)
//   - Atomic add to combined PnL
//   - If PnL < -limit, tripped flag set
//   - Once tripped, BOTH engines stop trading
//
// PER ARCHITECTURE.MD:
//   "The ONLY place engines interact is DailyLossGuard"
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// DailyLossGuard - Atomic cross-engine PnL guard
// ─────────────────────────────────────────────────────────────────────────────
class DailyLossGuard {
public:
    // Default limit: -$500 NZD
    static constexpr double DEFAULT_LIMIT_NZD = -500.0;
    
    explicit DailyLossGuard(double limit_nzd = DEFAULT_LIMIT_NZD) noexcept
        : limit_nzd_(limit_nzd)
        , pnl_nzd_(0.0)
        , tripped_(false)
        , trip_ts_ns_(0)
    {}
    
    // ═══════════════════════════════════════════════════════════════════════
    // HOT PATH - Called on every fill by both engines
    // ═══════════════════════════════════════════════════════════════════════
    
    // Check if trading is allowed (fast read)
    [[nodiscard]] inline bool allow() const noexcept {
        return !tripped_.load(std::memory_order_relaxed);
    }
    
    // Record a fill PnL (atomic add)
    inline void on_fill(double pnl_nzd) noexcept {
        // Atomic add using compare-exchange loop
        double current = pnl_nzd_.load(std::memory_order_relaxed);
        double desired;
        do {
            desired = current + pnl_nzd;
        } while (!pnl_nzd_.compare_exchange_weak(
            current, desired,
            std::memory_order_release,
            std::memory_order_relaxed));
        
        // Check if we hit the limit
        if (desired < limit_nzd_ && !tripped_.load(std::memory_order_relaxed)) {
            trip_ts_ns_.store(now_ns(), std::memory_order_relaxed);
            tripped_.store(true, std::memory_order_release);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // COLD PATH - Accessors for monitoring
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] double pnl() const noexcept {
        return pnl_nzd_.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] double limit() const noexcept {
        return limit_nzd_;
    }
    
    [[nodiscard]] bool tripped() const noexcept {
        return tripped_.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] uint64_t trip_timestamp() const noexcept {
        return trip_ts_ns_.load(std::memory_order_relaxed);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DRAWDOWN THROTTLE - For unified risk scaler
    // ═══════════════════════════════════════════════════════════════════════
    
    // Returns DD_used = |DD_current| / |DD_max| in [0, 1]
    [[nodiscard]] double drawdown_used() const noexcept {
        double current_pnl = pnl_nzd_.load(std::memory_order_relaxed);
        if (current_pnl >= 0.0 || limit_nzd_ >= 0.0) return 0.0;
        return std::min(1.0, std::fabs(current_pnl) / std::fabs(limit_nzd_));
    }
    
    // Returns remaining buffer as fraction [0, 1]
    [[nodiscard]] double buffer_remaining() const noexcept {
        return 1.0 - drawdown_used();
    }
    
    // Returns risk throttle factor Q_dd = (1 - DD_used^exp)
    [[nodiscard]] double throttle_factor(double exponent = 2.0) const noexcept {
        double dd = drawdown_used();
        return std::max(0.0, 1.0 - std::pow(dd, exponent));
    }
    
    // Reset for new trading day (call from main thread only, when engines stopped)
    void reset() noexcept {
        pnl_nzd_.store(0.0, std::memory_order_relaxed);
        tripped_.store(false, std::memory_order_relaxed);
        trip_ts_ns_.store(0, std::memory_order_relaxed);
    }
    
    // Non-copyable
    DailyLossGuard(const DailyLossGuard&) = delete;
    DailyLossGuard& operator=(const DailyLossGuard&) = delete;

private:
    static uint64_t now_ns() noexcept {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
    
    const double limit_nzd_;
    
    alignas(64) std::atomic<double>   pnl_nzd_;
    alignas(64) std::atomic<bool>     tripped_;
    alignas(64) std::atomic<uint64_t> trip_ts_ns_;
};

} // namespace Chimera
