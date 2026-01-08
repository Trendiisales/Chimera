// =============================================================================
// PortfolioModeController.hpp - v4.10.3 - PORTFOLIO WITH GOLD SCALE GUARD
// =============================================================================
// HARD LOCKS (v4.10.3):
//   ✅ INDICES: NAS100, US30 (fixed risk)
//   ✅ GOLD: XAUUSD (discipline-gated scaling via GoldScaleGuard)
//
//   ✅ INDEX RISK: FIXED
//      NAS100 = 0.5%
//      US30 = 0.4%
//
//   ✅ GOLD RISK: SCALE-GUARDED
//      MICRO = 0.10% (default, must prove discipline)
//      LEVEL_1 = 0.20% (after 30 trades with metrics passing)
//      LEVEL_2 = 0.30% (after 60 trades with continued discipline)
//
//   ✅ MODE: INDEX_PRIORITY (frozen, no switching)
//   ✅ DAILY LOSS HALT: -2.0R
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>

#include "portfolio/GoldScaleGuard.hpp"

namespace Chimera {

// =============================================================================
// ALLOWED SYMBOLS (v4.10.3: Indices + Gold)
// =============================================================================
inline bool isAllowedSymbol(const char* symbol) {
    return (strstr(symbol, "NAS100") != nullptr || 
            strstr(symbol, "US30") != nullptr ||
            strstr(symbol, "XAUUSD") != nullptr);
}

inline bool isIndexSymbol(const char* symbol) {
    return (strstr(symbol, "NAS100") != nullptr || 
            strstr(symbol, "US30") != nullptr);
}

inline bool isGoldSymbol(const char* symbol) {
    return strstr(symbol, "XAUUSD") != nullptr;
}

// =============================================================================
// FIXED RISK FOR INDICES (Gold uses GoldScaleGuard)
// =============================================================================
static constexpr double NAS100_RISK = 0.005;  // 0.5% fixed
static constexpr double US30_RISK = 0.004;    // 0.4% fixed

inline double getFixedRisk(const char* symbol) {
    if (strstr(symbol, "NAS100") != nullptr) return NAS100_RISK;
    if (strstr(symbol, "US30") != nullptr) return US30_RISK;
    // Gold risk is handled by GoldScaleGuard, not here
    return 0.0;
}

// =============================================================================
// PORTFOLIO MODE
// =============================================================================
enum class PortfolioMode : uint8_t {
    INDEX_PRIORITY = 0,    // Indices have priority, Gold blocked when active
    GOLD_ALLOWED   = 1,    // Gold can trade (when indices inactive)
    HALTED         = 3     // Emergency stop
};

inline const char* portfolioModeStr(PortfolioMode m) {
    switch (m) {
        case PortfolioMode::INDEX_PRIORITY: return "INDEX_PRIORITY";
        case PortfolioMode::GOLD_ALLOWED:   return "GOLD_ALLOWED";
        case PortfolioMode::HALTED:         return "HALTED";
        default:                            return "UNKNOWN";
    }
}

// =============================================================================
// GOLD PERMISSION (Output from canTradeGold())
// =============================================================================
struct GoldPermission {
    bool allowed = false;
    double risk_pct = 0.0;
    const char* reason = "";
    
    void print() const {
        printf("[PORTFOLIO] GOLD_PERMISSION=%s risk=%.2f%% reason=%s\n",
               allowed ? "GRANTED" : "DENIED",
               risk_pct * 100.0,
               reason);
    }
};

// =============================================================================
// SYMBOL ALLOCATION
// =============================================================================
struct SymbolAllocation {
    char symbol[16] = {0};
    double risk = 0.0;
    bool enabled = false;
    const char* disable_reason = "";
};

// =============================================================================
// PORTFOLIO CONFIG (v4.10.2 - MINIMAL)
// =============================================================================
struct PortfolioConfig {
    // Daily loss halt (in R-multiple of total risk)
    double daily_halt_r = 2.0;   // Halt at -2R total daily loss
    
    // This means: if you risk 0.5% per trade,
    // -2R = -1.0% of equity triggers halt
};

// =============================================================================
// PORTFOLIO STATE
// =============================================================================
struct PortfolioState {
    PortfolioMode mode = PortfolioMode::INDEX_PRIORITY;
    bool mode_locked = true;  // v4.10.2: Always locked
    
    // Equity tracking
    double starting_equity = 0.0;
    double current_equity = 0.0;
    double daily_pnl = 0.0;
    double daily_pnl_pct = 0.0;
    
    // R-tracking
    double daily_pnl_r = 0.0;   // Daily P&L in R-multiples
    double total_risk_taken = 0.0;  // Sum of all trade risks today
    
    // Symbol tracking
    static constexpr size_t MAX_SYMBOLS = 5;  // NAS100, US30, XAUUSD + buffer
    std::array<SymbolAllocation, MAX_SYMBOLS> allocations{};
    size_t symbol_count = 0;
    
    void resetDaily(double equity) {
        mode = PortfolioMode::INDEX_PRIORITY;
        mode_locked = true;
        starting_equity = equity;
        current_equity = equity;
        daily_pnl = 0.0;
        daily_pnl_pct = 0.0;
        daily_pnl_r = 0.0;
        total_risk_taken = 0.0;
    }
};

// =============================================================================
// PORTFOLIO MODE CONTROLLER (v4.10.3 - WITH GOLD SCALE GUARD)
// =============================================================================
class PortfolioModeController {
public:
    PortfolioModeController() = default;
    
    void setConfig(const PortfolioConfig& cfg) { config_ = cfg; }
    const PortfolioConfig& config() const { return config_; }
    
    // =========================================================================
    // INITIALIZE DAY
    // =========================================================================
    void initDay(double equity) {
        state_.resetDaily(equity);
        
        printf("[PORTFOLIO] Day initialized: equity=$%.2f\n", equity);
        printf("[PORTFOLIO] Mode locked: %s\n", portfolioModeStr(state_.mode));
        printf("[PORTFOLIO] Index Risk: NAS100=%.1f%% US30=%.1f%% (FIXED)\n",
               NAS100_RISK * 100.0, US30_RISK * 100.0);
        printf("[PORTFOLIO] Gold Risk: %.2f%% (%s)\n",
               gold_scale_.getRiskPct() * 100.0, gold_scale_.getStatus());
        printf("[PORTFOLIO] Daily loss limit = -%.1fR\n", config_.daily_halt_r);
    }
    
    // =========================================================================
    // REGISTER SYMBOL (v4.10.3 - Indices + Gold)
    // =========================================================================
    void registerSymbol(const char* symbol) {
        // Hard reject non-allowed symbols
        if (!isAllowedSymbol(symbol)) {
            printf("[PORTFOLIO] REJECTED: %s (not in allowed list)\n", symbol);
            return;
        }
        
        // Check if already registered
        for (size_t i = 0; i < state_.symbol_count; i++) {
            if (strcmp(state_.allocations[i].symbol, symbol) == 0) {
                return;
            }
        }
        
        if (state_.symbol_count >= PortfolioState::MAX_SYMBOLS) {
            printf("[PORTFOLIO] WARNING: Max symbols reached\n");
            return;
        }
        
        auto& alloc = state_.allocations[state_.symbol_count++];
        strncpy(alloc.symbol, symbol, 15);
        
        // Gold uses scale guard, indices use fixed risk
        if (isGoldSymbol(symbol)) {
            alloc.risk = gold_scale_.getRiskPct();
            alloc.enabled = true;
            printf("[PORTFOLIO] Registered: %s risk=%.2f%% (SCALE-GUARDED)\n", 
                   symbol, alloc.risk * 100.0);
        } else {
            alloc.risk = getFixedRisk(symbol);
            alloc.enabled = true;
            printf("[PORTFOLIO] Registered: %s risk=%.1f%% (FIXED)\n", 
                   symbol, alloc.risk * 100.0);
        }
    }
    
    // =========================================================================
    // GOLD SCALE GUARD ACCESS
    // =========================================================================
    portfolio::GoldScaleGuard& goldScaleGuard() { return gold_scale_; }
    const portfolio::GoldScaleGuard& goldScaleGuard() const { return gold_scale_; }
    
    // Record Gold trade for scale evaluation
    void recordGoldTrade(const portfolio::GoldTradeStats& stats) {
        gold_scale_.recordTrade(stats);
        
        // Update Gold allocation risk after each trade
        for (size_t i = 0; i < state_.symbol_count; i++) {
            if (isGoldSymbol(state_.allocations[i].symbol)) {
                state_.allocations[i].risk = gold_scale_.getRiskPct();
            }
        }
    }
    
    // Get Gold risk (scale-guarded)
    [[nodiscard]] double getGoldRiskPct() const {
        return gold_scale_.getRiskPct();
    }
    
    // Check if Gold scaling is allowed
    [[nodiscard]] bool isGoldScaleAllowed() const {
        return gold_scale_.scaleAllowed();
    }
    
    // =========================================================================
    // GOLD PERMISSION GATE (SINGLE AUTHORITY - NON-NEGOTIABLE)
    // =========================================================================
    // Gold trading is allowed ONLY when ALL conditions are true:
    //   1) Portfolio mode allows it (not INDEX_PRIORITY or HALTED while indices active)
    //   2) Index engines are INACTIVE
    //   3) Gold campaign is ACTIVE
    //   4) Gold scale guard permits it (always allows MICRO, requires discipline for more)
    // =========================================================================
    
    void setIndexActive(bool active) {
        index_active_ = active;
        if (active && gold_campaign_active_) {
            printf("[PORTFOLIO] Index active - Gold blocked\n");
        }
    }
    
    void setGoldCampaignActive(bool active) {
        gold_campaign_active_ = active;
    }
    
    [[nodiscard]] bool isIndexActive() const { return index_active_; }
    [[nodiscard]] bool isGoldCampaignActive() const { return gold_campaign_active_; }
    
    // THE CRITICAL FUNCTION - SINGLE SOURCE OF TRUTH FOR GOLD PERMISSION
    [[nodiscard]] GoldPermission canTradeGold() const {
        // Check 1: Portfolio halted
        if (state_.mode == PortfolioMode::HALTED) {
            return denyGold("PORTFOLIO_HALTED");
        }
        
        // Check 2: Index active (HARD BLOCK)
        if (index_active_) {
            return denyGold("INDEX_ACTIVE");
        }
        
        // Check 3: Campaign not active
        if (!gold_campaign_active_) {
            return denyGold("NO_ACTIVE_CAMPAIGN");
        }
        
        // Check 4: Scale guard - always allows MICRO, requires discipline for more
        double risk = gold_scale_.getRiskPct();
        const char* reason = gold_scale_.scaleAllowed() ? "SCALE_ALLOWED" : "MICRO_ONLY";
        
        return { true, risk, reason };
    }
    
    void allowGold() {
        state_.mode = PortfolioMode::GOLD_ALLOWED;
        printf("[PORTFOLIO] Mode: GOLD_ALLOWED\n");
    }
    
    void lockToIndexPriority() {
        state_.mode = PortfolioMode::INDEX_PRIORITY;
        printf("[PORTFOLIO] Mode: INDEX_PRIORITY (Gold blocked while indices active)\n");
    }
    
    // =========================================================================
    // MODE DECISION (v4.10.3 - NO-OP, ALWAYS INDEX_PRIORITY)
    // =========================================================================
    void decideMode() {
        // Mode is frozen
        printf("[PORTFOLIO] Mode decision: %s (frozen)\n", portfolioModeStr(state_.mode));
    }
    
    // =========================================================================
    // UPDATE P&L
    // =========================================================================
    void updatePnL(double pnl_dollars, double risk_dollars) {
        state_.daily_pnl += pnl_dollars;
        state_.current_equity = state_.starting_equity + state_.daily_pnl;
        
        if (state_.starting_equity > 0.0) {
            state_.daily_pnl_pct = state_.daily_pnl / state_.starting_equity;
        }
        
        // Track R-multiple (using average risk per trade)
        if (risk_dollars > 0.0) {
            state_.total_risk_taken += risk_dollars;
            state_.daily_pnl_r = state_.daily_pnl / (state_.total_risk_taken / 
                (state_.total_risk_taken > 0 ? 1.0 : 1.0));  // Simplified R calc
        }
        
        // Check daily halt
        checkDailyHalt();
    }
    
    // =========================================================================
    // GET RISK FOR SYMBOL (Indices fixed, Gold scale-guarded)
    // =========================================================================
    [[nodiscard]] double getRisk(const char* symbol) const {
        if (state_.mode == PortfolioMode::HALTED) return 0.0;
        
        // Gold uses scale guard
        if (isGoldSymbol(symbol)) {
            return gold_scale_.getRiskPct();
        }
        
        // Indices use fixed risk from allocation
        for (size_t i = 0; i < state_.symbol_count; i++) {
            if (strcmp(state_.allocations[i].symbol, symbol) == 0) {
                if (!state_.allocations[i].enabled) return 0.0;
                return state_.allocations[i].risk;
            }
        }
        return 0.0;
    }
    
    // =========================================================================
    // CHECK IF SYMBOL ENABLED
    // =========================================================================
    [[nodiscard]] bool isSymbolEnabled(const char* symbol) const {
        if (state_.mode == PortfolioMode::HALTED) return false;
        if (!isAllowedSymbol(symbol)) return false;
        
        for (size_t i = 0; i < state_.symbol_count; i++) {
            if (strcmp(state_.allocations[i].symbol, symbol) == 0) {
                return state_.allocations[i].enabled;
            }
        }
        return false;
    }
    
    // =========================================================================
    // GET DISABLE REASON
    // =========================================================================
    [[nodiscard]] const char* getDisableReason(const char* symbol) const {
        if (!isAllowedSymbol(symbol)) return "SYMBOL_NOT_ALLOWED";
        if (state_.mode == PortfolioMode::HALTED) return "PORTFOLIO_HALTED";
        
        for (size_t i = 0; i < state_.symbol_count; i++) {
            if (strcmp(state_.allocations[i].symbol, symbol) == 0) {
                return state_.allocations[i].disable_reason;
            }
        }
        return "NOT_REGISTERED";
    }
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    [[nodiscard]] PortfolioMode mode() const { return state_.mode; }
    [[nodiscard]] bool isModeLocked() const { return state_.mode_locked; }
    [[nodiscard]] bool isHalted() const { return state_.mode == PortfolioMode::HALTED; }
    [[nodiscard]] double dailyPnLPct() const { return state_.daily_pnl_pct; }
    [[nodiscard]] double dailyPnLR() const { return state_.daily_pnl_r; }
    [[nodiscard]] const PortfolioState& getState() const { return state_; }
    
    // v4.10.2: Always INDEX_PRIORITY, time-independent
    [[nodiscard]] bool isModeDecisionTime() const { return false; }
    
    // =========================================================================
    // STATUS
    // =========================================================================
    void printStatus() const {
        printf("[PORTFOLIO] Status (v4.10.3):\n");
        printf("  Mode: %s (frozen)\n", portfolioModeStr(state_.mode));
        printf("  Equity: $%.2f (start=$%.2f)\n",
               state_.current_equity, state_.starting_equity);
        printf("  Daily P&L: $%.2f (%.2f%%)\n", 
               state_.daily_pnl, state_.daily_pnl_pct * 100.0);
        printf("  Daily loss limit: -%.1fR\n", config_.daily_halt_r);
        
        printf("  Registered symbols:\n");
        for (size_t i = 0; i < state_.symbol_count; i++) {
            const auto& a = state_.allocations[i];
            if (isGoldSymbol(a.symbol)) {
                printf("    %s: risk=%.2f%% %s [%s]\n",
                       a.symbol,
                       gold_scale_.getRiskPct() * 100.0,
                       a.enabled ? "ENABLED" : a.disable_reason,
                       gold_scale_.getStatus());
            } else {
                printf("    %s: risk=%.1f%% %s\n",
                       a.symbol,
                       a.risk * 100.0,
                       a.enabled ? "ENABLED" : a.disable_reason);
            }
        }
        
        // Print Gold scale guard status
        gold_scale_.printStatus();
    }

private:
    PortfolioConfig config_;
    PortfolioState state_;
    portfolio::GoldScaleGuard gold_scale_;  // Discipline-based Gold scaling
    
    // Gold permission gate state
    bool index_active_ = false;          // Are indices currently trading?
    bool gold_campaign_active_ = false;  // Is Gold campaign ACTIVE?
    
    // Helper for denied Gold permission
    static GoldPermission denyGold(const char* reason) {
        return { false, 0.0, reason };
    }
    
    // =========================================================================
    // CHECK DAILY HALT
    // =========================================================================
    void checkDailyHalt() {
        // Calculate effective R loss
        // Using average risk as 1R
        double avg_risk = (NAS100_RISK + US30_RISK) / 2.0;
        double loss_r = -state_.daily_pnl_pct / avg_risk;
        
        if (loss_r >= config_.daily_halt_r) {
            if (state_.mode != PortfolioMode::HALTED) {
                printf("[PORTFOLIO] DAILY HALT: Loss=%.2fR exceeds limit=%.1fR\n",
                       loss_r, config_.daily_halt_r);
                state_.mode = PortfolioMode::HALTED;
                
                // Disable all symbols
                for (size_t i = 0; i < state_.symbol_count; i++) {
                    state_.allocations[i].enabled = false;
                    state_.allocations[i].disable_reason = "DAILY_HALT";
                }
            }
        }
    }
};

// =============================================================================
// GLOBAL PORTFOLIO MODE CONTROLLER ACCESSOR
// =============================================================================
inline PortfolioModeController& getPortfolioController() {
    static PortfolioModeController instance;
    return instance;
}

// =============================================================================
// LEGACY COMPATIBILITY (v4.10.2)
// These types exist but are not used in locked mode
// =============================================================================
struct GoldModeSignal {
    bool asia_data_ready = false;
};

// NOTE: SymbolClass is defined in BringUpSystem.hpp (CFD, FX)
// Use Chimera::getSymbolClass() from there for classification

} // namespace Chimera
