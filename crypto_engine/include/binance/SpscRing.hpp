#pragma once
#include <atomic>
#include <cstddef>

namespace binance {

/*
 Single-producer / single-consumer lock-free ring buffer.
 Capacity must be power of two.
*/
template<typename T, size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

    T buffer[Capacity];
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};

public:
    bool push(const T& v) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t next = (h + 1) & (Capacity - 1);
        if (next == tail.load(std::memory_order_acquire))
            return false; // full
        buffer[h] = v;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire))
            return false; // empty
        out = buffer[t];
        tail.store((t + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head.load() == tail.load();
    }
};

}
