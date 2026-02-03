#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace chimera {

// ---------------------------------------------------------------------------
// TradeSignal: Fixed-size ABI struct for lock-free communication
// Strategies push these to the ring. ExecutionRouter pops and processes.
// 
// Design: Cacheline-aligned, power-of-2 ring buffer with MPSC semantics.
// Multiple producers (strategies), single consumer (router thread).
// ---------------------------------------------------------------------------

struct TradeSignal {
    char symbol[12];     // ETHUSDT\0 = 8 bytes, plenty
    char engine_id[12];  // ENGINE_ID\0
    double qty;
    double price;
    double edge_bps;
    uint64_t ts_submit;
    bool reduce_only;
    uint8_t padding[7];  // Align to 64 bytes
};

static_assert(sizeof(TradeSignal) == 64, "TradeSignal must be 64 bytes (cacheline)");

// ---------------------------------------------------------------------------
// SignalRing: Lock-free MPSC queue
// 
// Based on Dmitry Vyukov's bounded MPSC queue.
// Capacity must be power of 2.
// 
// Properties:
//   - Wait-free for consumer (router)
//   - Lock-free for producers (strategies)
//   - No heap allocation
//   - Cacheline-aligned head/tail to prevent false sharing
// ---------------------------------------------------------------------------

template<int N>
class SignalRing {
public:
    SignalRing() {
        static_assert((N & (N - 1)) == 0, "N must be power of 2");
        
        // Initialize sequence numbers for MPSC protocol
        for (size_t i = 0; i < N; ++i) {
            seq[i].store(i, std::memory_order_relaxed);
        }
    }

    // Producer (strategy thread) - push signal to ring
    // Returns false if ring is full (backpressure)
    bool push(const TradeSignal& s) {
        size_t pos = head.load(std::memory_order_relaxed);
        
        for (;;) {
            size_t idx = pos & mask;
            size_t s_val = seq[idx].load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)s_val - (intptr_t)pos;
            
            if (dif == 0) {
                // Slot available - try to claim it
                if (head.compare_exchange_weak(pos, pos + 1, 
                        std::memory_order_relaxed)) {
                    buffer[idx] = s;
                    seq[idx].store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                // Ring full - backpressure
                return false;
            } else {
                // Another producer claimed this slot - retry
                pos = head.load(std::memory_order_relaxed);
            }
        }
    }

    // Consumer (router thread) - pop signal from ring
    // Returns false if ring is empty
    bool pop(TradeSignal& out) {
        size_t pos = tail.load(std::memory_order_relaxed);
        size_t idx = pos & mask;
        size_t s_val = seq[idx].load(std::memory_order_acquire);
        intptr_t dif = (intptr_t)s_val - (intptr_t)(pos + 1);
        
        if (dif == 0) {
            // Data available
            out = buffer[idx];
            seq[idx].store(pos + N, std::memory_order_release);
            tail.store(pos + 1, std::memory_order_release);
            return true;
        }
        
        return false;  // Empty
    }

private:
    static constexpr size_t mask = N - 1;

    // Cacheline-aligned to prevent false sharing
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    
    // Per-slot sequence numbers (MPSC protocol)
    alignas(64) std::atomic<size_t> seq[N];
    
    // Signal buffer
    alignas(64) TradeSignal buffer[N];
};

} // namespace chimera
