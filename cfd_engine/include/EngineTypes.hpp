// =============================================================================
// EngineTypes.hpp - Shared Types for Engine Architecture
// =============================================================================
// v4.12.0: CRYPTO REMOVED - CFD only
// =============================================================================
#pragma once

#include <cstdint>
#include <atomic>

namespace Omega {

// =============================================================================
// AggregatedSignal - Combined output from all strategies
// =============================================================================
struct AggregatedSignal {
    double totalValue = 0.0;
    double avgConfidence = 0.0;
    int buyCount = 0;
    int sellCount = 0;
    int neutralCount = 0;
    int8_t consensus = 0;  // -1, 0, +1
    uint64_t ts = 0;
    
    inline bool hasConsensus() const {
        return (buyCount > sellCount * 2) || (sellCount > buyCount * 2);
    }
    
    inline bool isStrongBuy() const {
        return consensus > 0 && avgConfidence > 0.5 && buyCount >= 20;
    }
    
    inline bool isStrongSell() const {
        return consensus < 0 && avgConfidence > 0.5 && sellCount >= 20;
    }
    
    inline void reset() {
        totalValue = 0.0;
        avgConfidence = 0.0;
        buyCount = 0;
        sellCount = 0;
        neutralCount = 0;
        consensus = 0;
        ts = 0;
    }
};

// =============================================================================
// Global Kill Switch - CFD only (v4.12.0)
// =============================================================================
struct GlobalKillSwitch {
    std::atomic<bool> kill_all{false};
    std::atomic<bool> kill_cfd{false};
    
    inline void triggerAll() {
        kill_all.store(true, std::memory_order_release);
    }
    
    inline void triggerCfd() {
        kill_cfd.store(true, std::memory_order_release);
    }
    
    inline void reset() {
        kill_all.store(false, std::memory_order_release);
        kill_cfd.store(false, std::memory_order_release);
    }
    
    inline bool isCfdKilled() const {
        return kill_all.load(std::memory_order_acquire) || 
               kill_cfd.load(std::memory_order_acquire);
    }
    
    inline bool isKilled() const {
        return isCfdKilled();
    }
};

} // namespace Omega
