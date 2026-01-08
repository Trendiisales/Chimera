#pragma once
// =============================================================================
// CHIMERA EXECUTION AUTHORITY - v4.12.0 - THE SINGLE CHOKE POINT
// NO ORDER MAY BE SENT WITHOUT PASSING THROUGH THIS GATE
// =============================================================================
// v4.12.0: CRYPTO REMOVED - CFD only (cTrader via FIX or OpenAPI)
//
// Uses unified enum definitions from ChimeraEnums.hpp
// Intent state is passed IN, not queried from a global
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Use unified enum definitions (LatencyState, ShockState)
#include "../shared/ChimeraEnums.hpp"

namespace Chimera {

// =============================================================================
// EXECUTION-SPECIFIC ENUMS (not shared elsewhere)
// =============================================================================
enum class ExecBlockReason : uint8_t {
    NONE = 0,
    INTENT_NOT_LIVE,
    SYMBOL_BLOCKED,
    FIX_NOT_CONNECTED,
    NO_EXPANSION,
    LATENCY_DEGRADED,
    SHOCK_DETECTED,
    RISK_BLOCKED,
    DAILY_LOSS_HIT,
    VENUE_UNHEALTHY,
    STRUCTURE_INVALID,
    COOLDOWN_ACTIVE
};

inline const char* execBlockReasonToString(ExecBlockReason r) {
    switch (r) {
        case ExecBlockReason::NONE:              return "READY";
        case ExecBlockReason::INTENT_NOT_LIVE:   return "INTENT_NOT_LIVE";
        case ExecBlockReason::SYMBOL_BLOCKED:    return "SYMBOL_BLOCKED";
        case ExecBlockReason::FIX_NOT_CONNECTED: return "FIX_NOT_CONNECTED";
        case ExecBlockReason::NO_EXPANSION:      return "NO_EXPANSION";
        case ExecBlockReason::LATENCY_DEGRADED:  return "LATENCY_DEGRADED";
        case ExecBlockReason::SHOCK_DETECTED:    return "SHOCK_DETECTED";
        case ExecBlockReason::RISK_BLOCKED:      return "RISK_BLOCKED";
        case ExecBlockReason::DAILY_LOSS_HIT:    return "DAILY_LOSS_HIT";
        case ExecBlockReason::VENUE_UNHEALTHY:   return "VENUE_UNHEALTHY";
        case ExecBlockReason::STRUCTURE_INVALID: return "STRUCTURE_INVALID";
        case ExecBlockReason::COOLDOWN_ACTIVE:   return "COOLDOWN_ACTIVE";
        default:                                 return "UNKNOWN";
    }
}

// v4.12.0: CRYPTO tier removed
enum class SymbolTier : uint8_t {
    PRIMARY = 0, 
    SECONDARY = 1, 
    OPPORTUNISTIC = 2, 
    BLOCKED = 3
};

// v4.12.0: Crypto symbols removed from classification
inline SymbolTier classifySymbolTier(const char* symbol) {
    if (!symbol) return SymbolTier::BLOCKED;
    if (strcmp(symbol, "NAS100") == 0 || strcmp(symbol, "XAUUSD") == 0) return SymbolTier::PRIMARY;
    if (strcmp(symbol, "US30") == 0 || strcmp(symbol, "SPX500") == 0) return SymbolTier::SECONDARY;
    if (strcmp(symbol, "EURUSD") == 0 || strcmp(symbol, "GBPUSD") == 0 || 
        strcmp(symbol, "USDJPY") == 0 || strcmp(symbol, "XAGUSD") == 0) return SymbolTier::OPPORTUNISTIC;
    return SymbolTier::BLOCKED;
}

// =============================================================================
// EXECUTION AUTHORITY - THE GATE
// =============================================================================
class ExecutionAuthority {
public:
    static ExecutionAuthority& instance() {
        static ExecutionAuthority inst;
        return inst;
    }
    
    // =========================================================================
    // THE GATE - intent_is_live is passed in from caller
    // v4.12.0: CFD only, requires FIX/OpenAPI connection
    // =========================================================================
    bool allow(const char* symbol, bool fix_connected, bool expansion, 
               bool intent_is_live, ExecBlockReason* out_reason = nullptr) {
        
        // 1. INTENT CHECK - FIRST
        if (!intent_is_live) {
            if (out_reason) *out_reason = ExecBlockReason::INTENT_NOT_LIVE;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // 2. SYMBOL CHECK
        SymbolTier tier = classifySymbolTier(symbol);
        if (tier == SymbolTier::BLOCKED) {
            if (out_reason) *out_reason = ExecBlockReason::SYMBOL_BLOCKED;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // 3. FIX/OpenAPI CHECK - Required for all CFD
        if (!fix_connected) {
            if (out_reason) *out_reason = ExecBlockReason::FIX_NOT_CONNECTED;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // 4. EXPANSION CHECK (OPPORTUNISTIC only)
        if (tier == SymbolTier::OPPORTUNISTIC && !expansion) {
            if (out_reason) *out_reason = ExecBlockReason::NO_EXPANSION;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // 5. LATENCY CHECK
        if (latency_state_.load(std::memory_order_relaxed) == LatencyState::DEGRADED) {
            if (out_reason) *out_reason = ExecBlockReason::LATENCY_DEGRADED;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // 6. SHOCK CHECK
        if (shock_state_.load(std::memory_order_relaxed) != ShockState::CLEAR) {
            if (out_reason) *out_reason = ExecBlockReason::SHOCK_DETECTED;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // 7. RISK CHECK
        if (!risk_allows_.load(std::memory_order_relaxed)) {
            if (out_reason) *out_reason = ExecBlockReason::RISK_BLOCKED;
            blocked_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        if (out_reason) *out_reason = ExecBlockReason::NONE;
        allowed_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    // Convenience: CFD needs FIX/OpenAPI connection check
    bool allowCFD(const char* symbol, bool fix_connected, bool expansion, 
                  bool intent_is_live, ExecBlockReason* out_reason = nullptr) {
        return allow(symbol, fix_connected, expansion, intent_is_live, out_reason);
    }
    
    void setLatencyState(LatencyState s) { latency_state_.store(s, std::memory_order_relaxed); }
    void setShockState(ShockState s) { shock_state_.store(s, std::memory_order_relaxed); }
    void setRiskAllows(bool allows) { risk_allows_.store(allows, std::memory_order_relaxed); }
    
    LatencyState getLatencyState() const { return latency_state_.load(std::memory_order_relaxed); }
    ShockState getShockState() const { return shock_state_.load(std::memory_order_relaxed); }
    bool getRiskAllows() const { return risk_allows_.load(std::memory_order_relaxed); }
    uint64_t getAllowedCount() const { return allowed_count_.load(std::memory_order_relaxed); }
    uint64_t getBlockedCount() const { return blocked_count_.load(std::memory_order_relaxed); }

private:
    ExecutionAuthority() : latency_state_(LatencyState::NORMAL), shock_state_(ShockState::CLEAR),
                           risk_allows_(true), allowed_count_(0), blocked_count_(0) {}
    
    std::atomic<LatencyState> latency_state_;
    std::atomic<ShockState> shock_state_;
    std::atomic<bool> risk_allows_;
    std::atomic<uint64_t> allowed_count_;
    std::atomic<uint64_t> blocked_count_;
};

inline ExecutionAuthority& getExecutionAuthority() { return ExecutionAuthority::instance(); }

} // namespace Chimera
