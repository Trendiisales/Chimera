#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>

namespace chimera {
namespace infra {

template<typename T, size_t Capacity>
class SPSCRingBuffer {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    SPSCRingBuffer()
        : m_write_index(0)
        , m_read_index(0)
    {
    }

    bool try_push(const T& value)
    {
        const size_t current_write = m_write_index.load(std::memory_order_relaxed);
        const size_t next_write = increment(current_write);
        
        if (next_write == m_read_index.load(std::memory_order_acquire))
            return false; // Ring buffer is full

        m_buffer[current_write] = value;
        m_write_index.store(next_write, std::memory_order_release);
        
        return true;
    }

    bool try_pop(T& value)
    {
        const size_t current_read = m_read_index.load(std::memory_order_relaxed);
        
        if (current_read == m_write_index.load(std::memory_order_acquire))
            return false; // Ring buffer is empty

        value = m_buffer[current_read];
        m_read_index.store(increment(current_read), std::memory_order_release);
        
        return true;
    }

    size_t size() const
    {
        const size_t write = m_write_index.load(std::memory_order_acquire);
        const size_t read = m_read_index.load(std::memory_order_acquire);
        
        if (write >= read)
            return write - read;
        else
            return Capacity - (read - write);
    }

    bool is_empty() const
    {
        return m_read_index.load(std::memory_order_acquire) == 
               m_write_index.load(std::memory_order_acquire);
    }

    bool is_full() const
    {
        const size_t next_write = increment(m_write_index.load(std::memory_order_acquire));
        return next_write == m_read_index.load(std::memory_order_acquire);
    }

    size_t capacity() const
    {
        return Capacity - 1; // One slot always reserved to distinguish full from empty
    }

private:
    static constexpr size_t increment(size_t index)
    {
        return (index + 1) & (Capacity - 1);
    }

    alignas(64) std::atomic<size_t> m_write_index;
    alignas(64) std::atomic<size_t> m_read_index;
    std::array<T, Capacity> m_buffer;
};

// Specialized version for POD types with memcpy optimization
template<typename T, size_t Capacity>
class SPSCRingBufferPOD {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(std::is_trivially_copyable<T>::value, "T must be POD type");

    SPSCRingBufferPOD()
        : m_write_index(0)
        , m_read_index(0)
    {
    }

    bool try_push(const T& value)
    {
        const size_t current_write = m_write_index.load(std::memory_order_relaxed);
        const size_t next_write = increment(current_write);
        
        if (next_write == m_read_index.load(std::memory_order_acquire))
            return false;

        std::memcpy(&m_buffer[current_write], &value, sizeof(T));
        m_write_index.store(next_write, std::memory_order_release);
        
        return true;
    }

    bool try_pop(T& value)
    {
        const size_t current_read = m_read_index.load(std::memory_order_relaxed);
        
        if (current_read == m_write_index.load(std::memory_order_acquire))
            return false;

        std::memcpy(&value, &m_buffer[current_read], sizeof(T));
        m_read_index.store(increment(current_read), std::memory_order_release);
        
        return true;
    }

    size_t size() const
    {
        const size_t write = m_write_index.load(std::memory_order_acquire);
        const size_t read = m_read_index.load(std::memory_order_acquire);
        
        if (write >= read)
            return write - read;
        else
            return Capacity - (read - write);
    }

    bool is_empty() const
    {
        return m_read_index.load(std::memory_order_acquire) == 
               m_write_index.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t increment(size_t index)
    {
        return (index + 1) & (Capacity - 1);
    }

    alignas(64) std::atomic<size_t> m_write_index;
    alignas(64) std::atomic<size_t> m_read_index;
    alignas(64) std::array<T, Capacity> m_buffer;
};

} // namespace infra
} // namespace chimera
