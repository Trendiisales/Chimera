#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

/* =========================
   INTENT STRUCT
   ========================= */

struct Intent {
    enum Side : uint8_t {
        BUY  = 1,
        SELL = 2
    };

    Side        side;
    char        symbol[16];
    double      qty;
    uint64_t    ts_ns;

    Intent() = default;

    Intent(Side s, const char* sym, double q, uint64_t ts)
        : side(s), qty(q), ts_ns(ts)
    {
        std::memset(symbol, 0, sizeof(symbol));
        std::strncpy(symbol, sym, sizeof(symbol) - 1);
    }
};

/* =========================
   LOCK-FREE MPSC RING QUEUE
   ========================= */

template <size_t CAPACITY>
class alignas(CACHELINE_SIZE) IntentQueue {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
                  "CAPACITY must be power of two");

public:
    IntentQueue()
        : head_(0), tail_(0) {}

    IntentQueue(const IntentQueue&) = delete;
    IntentQueue& operator=(const IntentQueue&) = delete;

    /* -------------------------
       PRODUCER (LOCK-FREE)
       ------------------------- */
    inline bool push(const Intent& intent) noexcept {
        uint64_t tail = tail_.load(std::memory_order_relaxed);

        for (;;) {
            uint64_t head = head_.load(std::memory_order_acquire);
            if ((tail - head) >= CAPACITY)
                return false;

            if (tail_.compare_exchange_weak(
                    tail,
                    tail + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                buffer_[tail & MASK] = intent;
                return true;
            }
        }
    }

    /* -------------------------
       CONSUMER (SINGLE THREAD)
       ------------------------- */
    inline bool try_pop(Intent& out) noexcept {
        uint64_t head = head_.load(std::memory_order_relaxed);
        uint64_t tail = tail_.load(std::memory_order_acquire);

        if (head == tail)
            return false;

        out = buffer_[head & MASK];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    inline uint64_t size() const noexcept {
        return tail_.load(std::memory_order_acquire)
             - head_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t MASK = CAPACITY - 1;

    alignas(CACHELINE_SIZE) std::atomic<uint64_t> head_;
    alignas(CACHELINE_SIZE) std::atomic<uint64_t> tail_;

    alignas(CACHELINE_SIZE) Intent buffer_[CAPACITY];
};
