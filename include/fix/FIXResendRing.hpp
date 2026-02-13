#pragma once
// =============================================================================
// FIXResendRing.hpp - Lock-Free FIX Message Resend Buffer
// =============================================================================
// CHIMERA HFT - Zero-Allocation Resend Ring
// HOT PATH: store() - no allocation, no mutex
// COLD PATH: fetch() - for ResendRequest handling
// =============================================================================

#include <cstdint>
#include <atomic>
#include <cstring>

namespace Chimera {

// Single stored FIX message
struct alignas(64) FIXStoredMsg {
    uint32_t seq;                   // Sequence number
    uint32_t len;                   // Message length
    char     data[512];             // Message data
    
    FIXStoredMsg() : seq(0), len(0) {
        data[0] = '\0';
    }
};

// Lock-free ring buffer for FIX message resend
class FIXResendRing {
public:
    static constexpr uint32_t CAP = 4096;
    static constexpr uint32_t MASK = CAP - 1;
    
    FIXResendRing() : head_(0) {}
    
    // Store message - HOT PATH: No allocation, no mutex
    void store(uint32_t seq, const char* msg, uint32_t len) noexcept {
        uint32_t idx = seq & MASK;
        FIXStoredMsg& s = ring_[idx];
        
        uint32_t copy_len = len > sizeof(s.data) ? sizeof(s.data) : len;
        
        s.seq = seq;
        s.len = copy_len;
        std::memcpy(s.data, msg, copy_len);
        
        head_.store(seq, std::memory_order_release);
    }
    
    // Fetch message by sequence number
    bool fetch(uint32_t seq, FIXStoredMsg& out) const noexcept {
        uint32_t idx = seq & MASK;
        const FIXStoredMsg& s = ring_[idx];
        
        if (s.seq != seq) {
            return false;
        }
        
        out.seq = s.seq;
        out.len = s.len;
        std::memcpy(out.data, s.data, s.len);
        return true;
    }
    
    // Fetch range for ResendRequest
    uint32_t fetchRange(uint32_t begin, uint32_t end, 
                        FIXStoredMsg* out, uint32_t out_cap) const noexcept {
        if (begin > end || out_cap == 0) return 0;
        
        uint32_t count = 0;
        for (uint32_t seq = begin; seq <= end && count < out_cap; ++seq) {
            if (fetch(seq, out[count])) {
                ++count;
            }
        }
        return count;
    }
    
    uint32_t head() const noexcept {
        return head_.load(std::memory_order_acquire);
    }
    
    bool available(uint32_t seq) const noexcept {
        uint32_t h = head_.load(std::memory_order_acquire);
        if (seq > h) return false;
        if (h - seq >= CAP) return false;
        uint32_t idx = seq & MASK;
        return ring_[idx].seq == seq;
    }

private:
    alignas(64) FIXStoredMsg ring_[CAP];
    alignas(64) std::atomic<uint32_t> head_;
};

} // namespace Chimera
