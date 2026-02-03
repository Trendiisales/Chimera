#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>

namespace chimera {

// Cache line size for modern x86
static constexpr size_t CACHE_LINE_SIZE = 64;

// Aligned allocator for cache-line aligned data
template<typename T>
struct CacheAlignedAllocator {
    using value_type = T;
    
    T* allocate(size_t n) {
        void* ptr = nullptr;
        if (posix_memalign(&ptr, CACHE_LINE_SIZE, n * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(ptr);
    }
    
    void deallocate(T* ptr, size_t) {
        free(ptr);
    }
};

// Cache-aligned atomic for avoiding false sharing
template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAlignedAtomic {
    std::atomic<T> value;
    
    CacheAlignedAtomic() : value(T{}) {}
    CacheAlignedAtomic(T val) : value(val) {}
    
    T load(std::memory_order order = std::memory_order_seq_cst) const {
        return value.load(order);
    }
    
    void store(T val, std::memory_order order = std::memory_order_seq_cst) {
        value.store(val, order);
    }
    
    T fetch_add(T val, std::memory_order order = std::memory_order_seq_cst) {
        return value.fetch_add(val, order);
    }
};

// Hot-path signal structure (64 bytes = 1 cache line)
struct alignas(64) HotSignal {
    char symbol[12];      // 12 bytes
    char engine[12];      // 12 bytes
    double qty;           // 8 bytes
    double price;         // 8 bytes
    double edge;          // 8 bytes
    uint64_t timestamp;   // 8 bytes
    uint8_t flags;        // 1 byte
    uint8_t padding[7];   // 7 bytes padding to 64
    
    static_assert(sizeof(HotSignal) == 64, "HotSignal must be cache-line sized");
};

// Prefetch hint for data that will be accessed soon
inline void prefetch(const void* addr) {
    __builtin_prefetch(addr, 0, 3);  // Read, high temporal locality
}

// Prefetch for write
inline void prefetch_write(void* addr) {
    __builtin_prefetch(addr, 1, 3);  // Write, high temporal locality
}

} // namespace chimera
