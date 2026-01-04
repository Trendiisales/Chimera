// ═══════════════════════════════════════════════════════════════════════════════
// include/gui/NoTradeReason.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.12: STANDARDIZED NO-TRADE REASONS
//
// PURPOSE: When Chimera does nothing, the GUI must answer:
// "Which gate stopped trading, and why?"
//
// Institutions require this for:
// - Operator trust (no gaslighting)
// - Post-mortems
// - Audit trails
//
// CRITICAL: Only the FIRST blocker is shown (prevents confusion)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>

namespace Chimera {
namespace GUI {

// ─────────────────────────────────────────────────────────────────────────────
// No Trade Reason Enumeration
// ─────────────────────────────────────────────────────────────────────────────
enum class NoTradeReason : uint8_t {
    NONE = 0,                          // Trade IS allowed
    
    // Gate-level blocks (order matters - first blocker wins)
    SYSTEM_BOOTSTRAP        = 1,       // Still in bootstrap mode
    NEWS_HALT               = 2,       // Hard halt around news events
    PHYSICS_WAN_BLOCK       = 3,       // WAN physics blocks maker
    EXECUTION_NOT_FEASIBLE  = 4,       // Execution authority blocked
    REGIME_DEAD             = 5,       // Market has no structure
    ALPHA_NOT_VALID         = 6,       // Alpha conditions not met
    ALPHA_RETIRED           = 7,       // Alpha failed metrics, disabled
    SYMBOL_EXPECTANCY_NEG   = 8,       // Symbol expectancy negative
    SYMBOL_DISABLED         = 9,       // Symbol pruned from rotation
    SESSION_LOW_EXPECTANCY  = 10,      // Time-of-day expectancy too low
    RISK_BACKOFF            = 11,      // Risk governor throttling
    FAILURE_GOVERNOR        = 12,      // Too many failures
    DAILY_LOSS_CAP          = 13,      // Hit daily loss limit
    DRAWDOWN_LIMIT          = 14,      // Drawdown protection triggered
    GLOBAL_KILL             = 15,      // Kill switch active
    LATENCY_DEGRADED        = 16,      // Hot-path latency too high
    SPREAD_TOO_WIDE         = 17,      // Spread exceeds threshold
    POSITION_LIMIT          = 18,      // Max positions reached
    COOLDOWN_ACTIVE         = 19,      // Post-trade cooldown
    GOVERNOR_HEAT           = 20,      // Governor heat too high
    
    // Informational (not blocking)
    WAITING_FOR_SIGNAL      = 100,     // No signal, this is normal
    CONNECTED_WAITING       = 101,     // Connected, waiting for conditions
};

inline const char* noTradeReasonStr(NoTradeReason r) {
    switch (r) {
        case NoTradeReason::NONE:                   return "NONE";
        case NoTradeReason::SYSTEM_BOOTSTRAP:       return "BOOTSTRAP";
        case NoTradeReason::NEWS_HALT:              return "NEWS_HALT";
        case NoTradeReason::PHYSICS_WAN_BLOCK:      return "PHYSICS_WAN";
        case NoTradeReason::EXECUTION_NOT_FEASIBLE: return "EXEC_BLOCK";
        case NoTradeReason::REGIME_DEAD:            return "DEAD_MARKET";
        case NoTradeReason::ALPHA_NOT_VALID:        return "ALPHA_INVALID";
        case NoTradeReason::ALPHA_RETIRED:          return "ALPHA_RETIRED";
        case NoTradeReason::SYMBOL_EXPECTANCY_NEG:  return "NEG_EXPECT";
        case NoTradeReason::SYMBOL_DISABLED:        return "SYM_DISABLED";
        case NoTradeReason::SESSION_LOW_EXPECTANCY: return "SESSION_LOW";
        case NoTradeReason::RISK_BACKOFF:           return "RISK_BACKOFF";
        case NoTradeReason::FAILURE_GOVERNOR:       return "FAIL_GOV";
        case NoTradeReason::DAILY_LOSS_CAP:         return "DAILY_LOSS";
        case NoTradeReason::DRAWDOWN_LIMIT:         return "DRAWDOWN";
        case NoTradeReason::GLOBAL_KILL:            return "KILLED";
        case NoTradeReason::LATENCY_DEGRADED:       return "HIGH_LATENCY";
        case NoTradeReason::SPREAD_TOO_WIDE:        return "WIDE_SPREAD";
        case NoTradeReason::POSITION_LIMIT:         return "POS_LIMIT";
        case NoTradeReason::COOLDOWN_ACTIVE:        return "COOLDOWN";
        case NoTradeReason::GOVERNOR_HEAT:          return "GOV_HEAT";
        case NoTradeReason::WAITING_FOR_SIGNAL:     return "WAITING";
        case NoTradeReason::CONNECTED_WAITING:      return "CONNECTED";
        default: return "UNKNOWN";
    }
}

// Human-readable descriptions for GUI
inline const char* noTradeReasonDesc(NoTradeReason r) {
    switch (r) {
        case NoTradeReason::NONE:                   
            return "Trade allowed";
        case NoTradeReason::SYSTEM_BOOTSTRAP:       
            return "System measuring latency (probing)";
        case NoTradeReason::NEWS_HALT:              
            return "Hard halt around high-impact news";
        case NoTradeReason::PHYSICS_WAN_BLOCK:      
            return "WAN physics: maker not viable";
        case NoTradeReason::EXECUTION_NOT_FEASIBLE: 
            return "Execution authority blocked trade";
        case NoTradeReason::REGIME_DEAD:            
            return "No market structure - do not trade";
        case NoTradeReason::ALPHA_NOT_VALID:        
            return "Alpha conditions not satisfied";
        case NoTradeReason::ALPHA_RETIRED:          
            return "Alpha auto-retired due to poor metrics";
        case NoTradeReason::SYMBOL_EXPECTANCY_NEG:  
            return "Symbol expectancy is negative";
        case NoTradeReason::SYMBOL_DISABLED:        
            return "Symbol pruned from active rotation";
        case NoTradeReason::SESSION_LOW_EXPECTANCY: 
            return "Time-of-day expectancy too low";
        case NoTradeReason::RISK_BACKOFF:           
            return "Risk governor reducing activity";
        case NoTradeReason::FAILURE_GOVERNOR:       
            return "Too many failures, backing off";
        case NoTradeReason::DAILY_LOSS_CAP:         
            return "Daily loss limit reached";
        case NoTradeReason::DRAWDOWN_LIMIT:         
            return "Drawdown protection triggered";
        case NoTradeReason::GLOBAL_KILL:            
            return "Kill switch activated";
        case NoTradeReason::LATENCY_DEGRADED:       
            return "Hot-path latency degraded";
        case NoTradeReason::SPREAD_TOO_WIDE:        
            return "Spread exceeds maximum threshold";
        case NoTradeReason::POSITION_LIMIT:         
            return "Maximum positions reached";
        case NoTradeReason::COOLDOWN_ACTIVE:        
            return "Post-trade cooldown period";
        case NoTradeReason::GOVERNOR_HEAT:          
            return "Governor heat too high, reducing size";
        case NoTradeReason::WAITING_FOR_SIGNAL:     
            return "Normal: waiting for trade signal";
        case NoTradeReason::CONNECTED_WAITING:      
            return "Connected, waiting for conditions";
        default: return "Unknown block reason";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Severity Level (for GUI coloring)
// ─────────────────────────────────────────────────────────────────────────────
enum class BlockSeverity : uint8_t {
    NORMAL  = 0,  // Green - operating normally
    INFO    = 1,  // Blue - informational, not a problem
    WARNING = 2,  // Yellow - temporary issue
    ERROR   = 3,  // Red - significant problem
    FATAL   = 4   // Dark red - system halted
};

inline BlockSeverity getBlockSeverity(NoTradeReason r) {
    switch (r) {
        case NoTradeReason::NONE:
        case NoTradeReason::WAITING_FOR_SIGNAL:
        case NoTradeReason::CONNECTED_WAITING:
            return BlockSeverity::NORMAL;
            
        case NoTradeReason::SYSTEM_BOOTSTRAP:
        case NoTradeReason::COOLDOWN_ACTIVE:
        case NoTradeReason::REGIME_DEAD:
        case NoTradeReason::SESSION_LOW_EXPECTANCY:
            return BlockSeverity::INFO;
            
        case NoTradeReason::LATENCY_DEGRADED:
        case NoTradeReason::SPREAD_TOO_WIDE:
        case NoTradeReason::ALPHA_NOT_VALID:
        case NoTradeReason::RISK_BACKOFF:
        case NoTradeReason::GOVERNOR_HEAT:
        case NoTradeReason::POSITION_LIMIT:
            return BlockSeverity::WARNING;
            
        case NoTradeReason::PHYSICS_WAN_BLOCK:
        case NoTradeReason::EXECUTION_NOT_FEASIBLE:
        case NoTradeReason::SYMBOL_EXPECTANCY_NEG:
        case NoTradeReason::SYMBOL_DISABLED:
        case NoTradeReason::ALPHA_RETIRED:
        case NoTradeReason::FAILURE_GOVERNOR:
        case NoTradeReason::NEWS_HALT:
            return BlockSeverity::ERROR;
            
        case NoTradeReason::DAILY_LOSS_CAP:
        case NoTradeReason::DRAWDOWN_LIMIT:
        case NoTradeReason::GLOBAL_KILL:
            return BlockSeverity::FATAL;
            
        default:
            return BlockSeverity::WARNING;
    }
}

inline const char* severityStr(BlockSeverity s) {
    switch (s) {
        case BlockSeverity::NORMAL:  return "normal";
        case BlockSeverity::INFO:    return "info";
        case BlockSeverity::WARNING: return "warning";
        case BlockSeverity::ERROR:   return "error";
        case BlockSeverity::FATAL:   return "fatal";
        default: return "unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Block Duration Tracking
// ─────────────────────────────────────────────────────────────────────────────
struct NoTradeState {
    NoTradeReason reason = NoTradeReason::CONNECTED_WAITING;
    uint64_t blocked_since_ns = 0;    // When block started
    uint64_t duration_ns = 0;         // How long blocked
    char detail[64] = {0};            // Additional context
    char symbol[16] = {0};            // Which symbol (if per-symbol)
    
    // v4.9.12 HARDENING: Debounce to prevent GUI chatter
    static constexpr uint64_t DEBOUNCE_NS = 5ULL * 1000000000ULL;  // 5 seconds
    uint64_t last_change_ns = 0;      // When reason last changed
    NoTradeReason pending_reason = NoTradeReason::CONNECTED_WAITING;
    uint64_t pending_since_ns = 0;    // When pending reason started
    
    void update(NoTradeReason new_reason, uint64_t now_ns, 
                const char* new_detail = nullptr) {
        // v4.9.12: Debounced update - prevent rapid GUI changes
        // Rule: Hold current reason for at least DEBOUNCE_NS unless:
        //   1. New reason is more severe (priority upgrade)
        //   2. Current reason is WAITING/CONNECTED (allow quick transitions from idle)
        
        BlockSeverity current_sev = getBlockSeverity(reason);
        BlockSeverity new_sev = getBlockSeverity(new_reason);
        
        bool is_idle = (reason == NoTradeReason::WAITING_FOR_SIGNAL ||
                        reason == NoTradeReason::CONNECTED_WAITING ||
                        reason == NoTradeReason::NONE);
        
        bool is_priority_upgrade = (static_cast<uint8_t>(new_sev) > 
                                    static_cast<uint8_t>(current_sev));
        
        bool debounce_expired = (now_ns - last_change_ns >= DEBOUNCE_NS);
        
        // Track pending reason
        if (new_reason != pending_reason) {
            pending_reason = new_reason;
            pending_since_ns = now_ns;
        }
        
        // Decide whether to commit the change
        bool should_commit = false;
        
        if (is_idle) {
            // From idle: transition immediately to any blocking reason
            should_commit = true;
        } else if (is_priority_upgrade) {
            // Severity increased: commit immediately (don't hide problems)
            should_commit = true;
        } else if (debounce_expired) {
            // Debounce window passed: allow change
            should_commit = true;
        } else if (new_reason == reason) {
            // Same reason: no change needed, just update duration
            should_commit = false;
        }
        
        if (should_commit && new_reason != reason) {
            reason = new_reason;
            blocked_since_ns = now_ns;
            last_change_ns = now_ns;
        }
        
        duration_ns = now_ns - blocked_since_ns;
        
        if (new_detail) {
            strncpy(detail, new_detail, sizeof(detail) - 1);
        }
    }
    
    double durationSec() const {
        return static_cast<double>(duration_ns) / 1'000'000'000.0;
    }
    
    int durationMinutes() const {
        return static_cast<int>(durationSec() / 60.0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol No Trade State Manager
// ─────────────────────────────────────────────────────────────────────────────
class NoTradeStateManager {
public:
    static constexpr size_t MAX_SYMBOLS = 16;
    
    NoTradeState* get(const char* symbol) {
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(states_[i].symbol, symbol) == 0) {
                return &states_[i];
            }
        }
        if (count_ < MAX_SYMBOLS) {
            strncpy(states_[count_].symbol, symbol, 15);
            return &states_[count_++];
        }
        return nullptr;
    }
    
    void update(const char* symbol, NoTradeReason reason, uint64_t now_ns,
                const char* detail = nullptr) {
        auto* state = get(symbol);
        if (state) {
            state->update(reason, now_ns, detail);
        }
    }
    
    // Get the most severe block across all symbols
    NoTradeState getMostSevere() const {
        NoTradeState worst;
        BlockSeverity worst_sev = BlockSeverity::NORMAL;
        
        for (size_t i = 0; i < count_; i++) {
            auto sev = getBlockSeverity(states_[i].reason);
            if (static_cast<uint8_t>(sev) > static_cast<uint8_t>(worst_sev)) {
                worst = states_[i];
                worst_sev = sev;
            }
        }
        
        return worst;
    }
    
private:
    NoTradeState states_[MAX_SYMBOLS];
    size_t count_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global No Trade State (for dashboard)
// ─────────────────────────────────────────────────────────────────────────────
inline NoTradeStateManager& getNoTradeStateManager() {
    static NoTradeStateManager mgr;
    return mgr;
}

} // namespace GUI
} // namespace Chimera
