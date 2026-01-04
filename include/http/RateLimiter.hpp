#pragma once
// =============================================================================
// RateLimiter.hpp v4.2.2 - Token Bucket Rate Limiter
// =============================================================================
// Prevents HTTP abuse from affecting trading performance.
// Even async HTTP can be dangerous if clients spam it.
//
// Uses token bucket algorithm:
//   - Bucket refills at fixed rate
//   - Each request consumes one token
//   - Requests blocked when bucket empty
// =============================================================================

#include <atomic>
#include <array>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace Omega {

// =============================================================================
// TOKEN BUCKET RATE LIMITER
// =============================================================================
class RateLimiter {
public:
    // Create limiter with max_per_sec requests per second
    explicit RateLimiter(int max_per_sec)
        : capacity_(max_per_sec)
        , tokens_(max_per_sec)
        , last_refill_(std::chrono::steady_clock::now())
    {}
    
    // Try to consume a token. Returns true if allowed, false if rate limited.
    bool Allow() {
        Refill();
        
        int current = tokens_.load(std::memory_order_relaxed);
        while (current > 0) {
            if (tokens_.compare_exchange_weak(current, current - 1,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
                return true;
            }
        }
        
        blocked_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Force allow (bypass rate limit) - use sparingly
    void ForceAllow() {
        Refill();
        tokens_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // Get current token count
    int TokensAvailable() const {
        return tokens_.load(std::memory_order_relaxed);
    }
    
    // Get blocked request count
    uint64_t BlockedCount() const {
        return blocked_count_.load(std::memory_order_relaxed);
    }
    
    // Reset limiter
    void Reset() {
        tokens_.store(capacity_, std::memory_order_relaxed);
        blocked_count_.store(0, std::memory_order_relaxed);
        last_refill_ = std::chrono::steady_clock::now();
    }
    
private:
    void Refill() {
        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_refill_).count();
        
        // Refill every second
        if (dt >= 1000) {
            tokens_.store(capacity_, std::memory_order_relaxed);
            last_refill_ = now;
        }
    }
    
    int capacity_;
    std::atomic<int> tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    std::atomic<uint64_t> blocked_count_{0};
};

// =============================================================================
// SLIDING WINDOW RATE LIMITER - More precise, slightly higher overhead
// =============================================================================
class SlidingWindowRateLimiter {
public:
    static constexpr size_t WINDOW_SIZE = 60;  // 60 slots for 60-second window
    
    explicit SlidingWindowRateLimiter(int max_per_minute)
        : max_per_minute_(max_per_minute)
    {
        for (auto& slot : slots_) {
            slot.store(0, std::memory_order_relaxed);
        }
    }
    
    bool Allow() {
        auto now = std::chrono::steady_clock::now();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        size_t current_slot = sec % WINDOW_SIZE;
        
        // Clear old slot if we've moved to a new second
        if (last_second_ != sec) {
            slots_[current_slot].store(0, std::memory_order_relaxed);
            last_second_ = sec;
        }
        
        // Count requests in window
        int total = 0;
        for (size_t i = 0; i < WINDOW_SIZE; i++) {
            total += slots_[i].load(std::memory_order_relaxed);
        }
        
        if (total >= max_per_minute_) {
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        slots_[current_slot].fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    uint64_t BlockedCount() const {
        return blocked_count_.load(std::memory_order_relaxed);
    }
    
private:
    int max_per_minute_;
    std::array<std::atomic<int>, WINDOW_SIZE> slots_;
    std::atomic<int64_t> last_second_{0};
    std::atomic<uint64_t> blocked_count_{0};
};

// =============================================================================
// PER-ENDPOINT RATE LIMITERS
// =============================================================================
struct HttpRateLimiters {
    RateLimiter metrics{5};      // 5 req/sec for metrics
    RateLimiter dashboard{10};   // 10 req/sec for dashboard
    RateLimiter health{20};      // 20 req/sec for health checks
    RateLimiter api{2};          // 2 req/sec for control API
    
    void ResetAll() {
        metrics.Reset();
        dashboard.Reset();
        health.Reset();
        api.Reset();
    }
    
    void LogStats() const {
        std::cout << "[RATE-LIMIT] blocked: metrics=" << metrics.BlockedCount()
                  << " dashboard=" << dashboard.BlockedCount()
                  << " health=" << health.BlockedCount()
                  << " api=" << api.BlockedCount() << "\n";
    }
};

} // namespace Omega
