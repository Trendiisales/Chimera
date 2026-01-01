#pragma once
// =============================================================================
// CapitalAllocator.hpp v4.2.2 - Multi-Factor Capital Allocation
// =============================================================================
// Capital allocation is NOT linear or static.
//
// Formula (per Document 7):
//   Capital = BaseRisk
//           × SymbolMultiplier
//           × RegimeConfidence  
//           × LatencyScore
//           × ExecutionHealth
//           × DrawdownThrottle
//
// MANDATORY: Capital drops to ZERO under:
//   - Latency breach
//   - Kill-switch Level 3+
//   - Venue degradation
//   - Book desync
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <iostream>
#include <iomanip>
#include <cstring>
#include "KillSwitchLadder.hpp"
#include "../micro/MicrostructureProfiles.hpp"

namespace Omega {

// =============================================================================
// ALLOCATION FACTORS
// =============================================================================
struct AllocationFactors {
    double base_risk = 1.0;           // Base risk unit
    double symbol_mult = 1.0;         // Per-symbol multiplier
    double regime_confidence = 1.0;   // Regime classifier output
    double latency_score = 1.0;       // Latency health (0-1)
    double execution_health = 1.0;    // Fill rate, slippage (0-1)
    double drawdown_throttle = 1.0;   // Drawdown-based reduction (0-1)
    
    double compute() const {
        return base_risk 
             * symbol_mult 
             * regime_confidence 
             * latency_score 
             * execution_health 
             * drawdown_throttle;
    }
    
    void log(const char* symbol) const {
        std::cout << "[ALLOC " << symbol << "] "
                  << "base=" << std::fixed << std::setprecision(2) << base_risk
                  << " sym=" << symbol_mult
                  << " regime=" << regime_confidence
                  << " lat=" << latency_score
                  << " exec=" << execution_health
                  << " dd=" << drawdown_throttle
                  << " → " << compute() << "\n";
    }
};

// =============================================================================
// SYMBOL MULTIPLIER CALCULATOR
// Per-symbol risk based on microstructure profile
// =============================================================================
inline double ComputeSymbolMultiplier(const MicrostructureProfile& mp) {
    // BTC/indices get more capital per trade
    // XAU/FX get less capital but higher urgency
    
    double base = 1.0;
    
    // Depth resilience bonus (thick book = more capital)
    base *= (0.5 + mp.depth_resilience * 0.5);
    
    // Adverse selection penalty (toxic flow = less capital)
    base *= (1.0 - mp.adverse_selection_rate * 0.5);
    
    // Snapback penalty
    base *= (1.0 - mp.snapback_penalty * 0.3);
    
    return std::clamp(base, 0.3, 2.0);
}

// =============================================================================
// LATENCY SCORE CALCULATOR
// =============================================================================
inline double ComputeLatencyScore(double ema_latency_ms, double max_acceptable_ms) {
    if (ema_latency_ms > max_acceptable_ms) return 0.0;
    
    // Linear degradation from 1.0 at 0ms to 0.0 at max
    double score = 1.0 - (ema_latency_ms / max_acceptable_ms);
    return std::clamp(score, 0.0, 1.0);
}

// =============================================================================
// EXECUTION HEALTH CALCULATOR
// =============================================================================
inline double ComputeExecutionHealth(
    double fill_rate,      // Orders filled / orders sent
    double ema_slippage,   // EMA of slippage in bps
    double max_slippage    // Maximum acceptable slippage
) {
    // Fill rate component (weight 0.4)
    double fill_score = fill_rate * 0.4;
    
    // Slippage component (weight 0.6)
    double slip_score = (1.0 - std::min(ema_slippage / max_slippage, 1.0)) * 0.6;
    
    return std::clamp(fill_score + slip_score, 0.0, 1.0);
}

// =============================================================================
// DRAWDOWN THROTTLE CALCULATOR
// =============================================================================
inline double ComputeDrawdownThrottle(
    double session_pnl_bps,
    double max_drawdown_bps
) {
    if (session_pnl_bps >= 0) return 1.0;
    
    // Linear reduction from 1.0 at 0 to 0.0 at max_drawdown
    double dd_ratio = std::abs(session_pnl_bps) / std::abs(max_drawdown_bps);
    double throttle = 1.0 - std::min(dd_ratio, 1.0);
    
    return std::clamp(throttle, 0.0, 1.0);
}

// =============================================================================
// REGIME CONFIDENCE CALCULATOR
// =============================================================================
enum class CapitalRegime : uint8_t {
    UNKNOWN = 0,
    TRENDING = 1,
    RANGING = 2,
    VOLATILE = 3,
    TOXIC = 4
};

inline double ComputeRegimeConfidence(
    CapitalRegime regime,
    const MicrostructureProfile& mp
) {
    // Match regime to symbol's preferred regime type
    switch (regime) {
        case CapitalRegime::TRENDING:
            // Good for momentum symbols
            if (mp.regime_type == MicrostructureProfile::REGIME_MOMENTUM_BURST) return 1.0;
            if (mp.regime_type == MicrostructureProfile::REGIME_LIQUIDITY_CLIFF) return 0.8;
            return 0.6;
            
        case CapitalRegime::RANGING:
            // Bad for momentum, ok for reversion
            if (mp.regime_type == MicrostructureProfile::REGIME_STOP_RUN_REVERSION) return 0.7;
            if (mp.regime_type == MicrostructureProfile::REGIME_COMPRESSION_BREAKOUT) return 0.5;
            return 0.3;
            
        case CapitalRegime::VOLATILE:
            // Risky for everyone
            if (mp.regime_type == MicrostructureProfile::REGIME_CHOP_IMPULSE) return 0.6;
            return 0.4;
            
        case CapitalRegime::TOXIC:
            // Toxic = no trading
            return 0.0;
            
        case CapitalRegime::UNKNOWN:
        default:
            return 0.5;  // Conservative default
    }
}

// =============================================================================
// CAPITAL ALLOCATOR - Complete multi-factor system
// =============================================================================
class CapitalAllocator {
public:
    struct SymbolAllocation {
        char symbol[16] = {0};
        AllocationFactors factors;
        double final_allocation = 0.0;
        bool is_zero = false;
        const char* zero_reason = "";
    };
    
    static constexpr size_t MAX_SYMBOLS = 30;
    
private:
    std::array<SymbolAllocation, MAX_SYMBOLS> allocations_;
    size_t symbol_count_ = 0;
    double base_risk_ = 1.0;
    double max_latency_ms_ = 20.0;
    double max_slippage_bps_ = 1.0;
    double max_drawdown_bps_ = 50.0;
    
public:
    void setBaseRisk(double base) { base_risk_ = base; }
    void setMaxLatency(double ms) { max_latency_ms_ = ms; }
    void setMaxSlippage(double bps) { max_slippage_bps_ = bps; }
    void setMaxDrawdown(double bps) { max_drawdown_bps_ = bps; }
    
    // Update allocation for a symbol
    void updateSymbol(
        const char* symbol,
        const MicrostructureProfile& mp,
        double ema_latency_ms,
        double ema_slippage_bps,
        double fill_rate,
        double session_pnl_bps,
        CapitalRegime regime,
        KillSwitchLevel ks_level,
        bool venue_degraded
    ) {
        // Find or create allocation
        SymbolAllocation* alloc = nullptr;
        for (size_t i = 0; i < symbol_count_; i++) {
            if (strcmp(allocations_[i].symbol, symbol) == 0) {
                alloc = &allocations_[i];
                break;
            }
        }
        if (!alloc && symbol_count_ < MAX_SYMBOLS) {
            alloc = &allocations_[symbol_count_++];
            strncpy(alloc->symbol, symbol, 15);
        }
        if (!alloc) return;
        
        // ═══════════════════════════════════════════════════════════════════
        // ZERO CAPITAL CONDITIONS (Document 7 mandatory)
        // ═══════════════════════════════════════════════════════════════════
        
        // Kill-switch Level 3+ = zero
        if (ks_level >= KillSwitchLevel::NO_NEW_ENTRY) {
            alloc->final_allocation = 0.0;
            alloc->is_zero = true;
            alloc->zero_reason = "KILL_SWITCH";
            return;
        }
        
        // Venue degradation = zero
        if (venue_degraded) {
            alloc->final_allocation = 0.0;
            alloc->is_zero = true;
            alloc->zero_reason = "VENUE_DEGRADED";
            return;
        }
        
        // Latency breach = zero
        double lat_score = ComputeLatencyScore(ema_latency_ms, max_latency_ms_);
        if (lat_score <= 0.0) {
            alloc->final_allocation = 0.0;
            alloc->is_zero = true;
            alloc->zero_reason = "LATENCY_BREACH";
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // COMPUTE ALL FACTORS
        // ═══════════════════════════════════════════════════════════════════
        alloc->is_zero = false;
        alloc->zero_reason = "";
        
        alloc->factors.base_risk = base_risk_;
        alloc->factors.symbol_mult = ComputeSymbolMultiplier(mp);
        alloc->factors.regime_confidence = ComputeRegimeConfidence(regime, mp);
        alloc->factors.latency_score = lat_score;
        alloc->factors.execution_health = ComputeExecutionHealth(
            fill_rate, ema_slippage_bps, max_slippage_bps_);
        alloc->factors.drawdown_throttle = ComputeDrawdownThrottle(
            session_pnl_bps, max_drawdown_bps_);
        
        alloc->final_allocation = alloc->factors.compute();
    }
    
    // Get current allocation for symbol
    double getAllocation(const char* symbol) const {
        for (size_t i = 0; i < symbol_count_; i++) {
            if (strcmp(allocations_[i].symbol, symbol) == 0) {
                return allocations_[i].final_allocation;
            }
        }
        return 0.0;
    }
    
    // Check if symbol has zero allocation
    bool isZero(const char* symbol, const char** reason = nullptr) const {
        for (size_t i = 0; i < symbol_count_; i++) {
            if (strcmp(allocations_[i].symbol, symbol) == 0) {
                if (reason) *reason = allocations_[i].zero_reason;
                return allocations_[i].is_zero;
            }
        }
        return true;  // Unknown symbol = zero
    }
    
    // Log all allocations
    void logAll() const {
        std::cout << "[CAPITAL ALLOCATOR] " << symbol_count_ << " symbols:\n";
        for (size_t i = 0; i < symbol_count_; i++) {
            const auto& a = allocations_[i];
            if (a.is_zero) {
                std::cout << "  " << a.symbol << ": ZERO (" << a.zero_reason << ")\n";
            } else {
                a.factors.log(a.symbol);
            }
        }
    }
};

// Global allocator singleton
inline CapitalAllocator& GetCapitalAllocator() {
    static CapitalAllocator allocator;
    return allocator;
}

} // namespace Omega
