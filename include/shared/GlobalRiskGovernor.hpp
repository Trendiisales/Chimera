// =============================================================================
// GlobalRiskGovernor.hpp - v4.5.1 - IMMUTABLE RISK FRAMEWORK
// =============================================================================
// PURPOSE: Unified risk control across all engines
//
// NON-NEGOTIABLE RULES:
//   1. Hard daily loss cap: -$200 NZD (nothing overrides this)
//   2. No daily profit cap (upside is open)
//   3. Per-engine risk limits are FIXED (no intraday changes)
//   4. Aggression scales via PERMISSION, not RISK
//
// DESIGN:
//   - Wraps DailyLossGuard for per-order enforcement
//   - Provides throttle curve for size scaling
//   - Tracks engine outcomes to control permission
//   - Auto-shutdown on multiple failure conditions
//
// RISK HIERARCHY:
//   IncomeEngine: 0.5% per trade  (sniper - rare, protected)
//   CFDEngine:    0.25% per trade (soldier - active, capped)
//   CryptoEngine: 0.05% per trade (opportunistic - kill on first loss)
//
// USAGE:
//   // At order submission:
//   if (!g_risk_governor.canSubmitOrder(EngineId::CFD)) return;
//
//   // For sizing:
//   double size = base_size * g_risk_governor.sizeMultiplier(EngineId::CFD);
// =============================================================================
#pragma once

#include <atomic>
#include <cstdio>
#include <cmath>
#include <chrono>
#include "DailyLossGuard.hpp"
#include "GlobalKill.hpp"
#include "../core/EngineOwnership.hpp"

namespace Chimera {

// =============================================================================
// Engine Risk Limits (FIXED - No intraday changes)
// =============================================================================
struct EngineRiskLimits {
    // Per-trade risk as percentage of capital
    static constexpr double INCOME_RISK_PCT = 0.50;   // 0.5% per trade
    static constexpr double CFD_RISK_PCT = 0.25;      // 0.25% per trade
    static constexpr double CRYPTO_RISK_PCT = 0.05;   // 0.05% per trade
    
    // Maximum simultaneous open risk (as percentage of capital)
    static constexpr double MAX_TOTAL_OPEN_RISK_PCT = 1.0;  // 1% max total
    
    // Trade count limits per engine per day
    static constexpr int INCOME_MAX_TRADES = 4;       // Sniper - few trades
    static constexpr int CFD_MAX_TRADES = 20;         // Soldier - more active
    static constexpr int CRYPTO_MAX_TRADES = 2;       // Kill on first loss anyway
    
    static double getRiskPct(EngineId engine) {
        switch (engine) {
            case EngineId::INCOME: return INCOME_RISK_PCT;
            case EngineId::CFD:    return CFD_RISK_PCT;
            case EngineId::BINANCE: return CRYPTO_RISK_PCT;
            default: return 0.0;
        }
    }
    
    static int getMaxTrades(EngineId engine) {
        switch (engine) {
            case EngineId::INCOME: return INCOME_MAX_TRADES;
            case EngineId::CFD:    return CFD_MAX_TRADES;
            case EngineId::BINANCE: return CRYPTO_MAX_TRADES;
            default: return 0;
        }
    }
};

// =============================================================================
// Aggression State - Determined by IncomeEngine outcome
// =============================================================================
enum class AggressionState : uint8_t {
    FULL,           // Income won → all engines full allocation
    REDUCED,        // Income scratched → CFD reduced, crypto disabled
    PROTECTION      // Income lost → all engines stand down
};

inline const char* aggression_str(AggressionState state) {
    switch (state) {
        case AggressionState::FULL:       return "FULL";
        case AggressionState::REDUCED:    return "REDUCED";
        case AggressionState::PROTECTION: return "PROTECTION";
        default:                          return "UNKNOWN";
    }
}

// =============================================================================
// Shutdown Reason
// =============================================================================
enum class ShutdownReason : uint8_t {
    NONE = 0,
    DAILY_LOSS_LIMIT,         // PnL <= -$200 NZD
    CONSECUTIVE_LOSSES,       // Two consecutive losses across engines
    LATENCY_DEGRADED,         // Execution latency too high
    OWNERSHIP_VIOLATION,      // Engine overlap
    MANUAL_PANIC              // Manual trigger
};

inline const char* shutdown_reason_str(ShutdownReason r) {
    switch (r) {
        case ShutdownReason::DAILY_LOSS_LIMIT:     return "DAILY_MAX_LOSS";
        case ShutdownReason::CONSECUTIVE_LOSSES:   return "CONSECUTIVE_LOSSES";
        case ShutdownReason::LATENCY_DEGRADED:     return "LATENCY_DEGRADED";
        case ShutdownReason::OWNERSHIP_VIOLATION:  return "OWNERSHIP_VIOLATION";
        case ShutdownReason::MANUAL_PANIC:         return "MANUAL_PANIC";
        default:                                   return "NONE";
    }
}

// =============================================================================
// Global Risk Governor (Singleton)
// =============================================================================
class GlobalRiskGovernor {
public:
    // =========================================================================
    // SINGLETON ACCESS
    // =========================================================================
    static GlobalRiskGovernor& instance() {
        static GlobalRiskGovernor inst;
        return inst;
    }
    
    // =========================================================================
    // INITIALIZATION
    // =========================================================================
    void init(DailyLossGuard* daily_loss, GlobalKill* kill_switch, double capital_nzd) {
        daily_loss_ = daily_loss;
        kill_switch_ = kill_switch;
        capital_nzd_ = capital_nzd;
        
        printf("[RISK-GOVERNOR] Initialized:\n");
        printf("  Daily loss cap: $%.0f NZD\n", daily_loss_->limit());
        printf("  Capital: $%.0f NZD\n", capital_nzd_);
        printf("  Income risk: %.2f%% = $%.2f/trade\n", 
               EngineRiskLimits::INCOME_RISK_PCT, 
               capital_nzd_ * EngineRiskLimits::INCOME_RISK_PCT / 100.0);
        printf("  CFD risk: %.2f%% = $%.2f/trade\n",
               EngineRiskLimits::CFD_RISK_PCT,
               capital_nzd_ * EngineRiskLimits::CFD_RISK_PCT / 100.0);
        printf("  Crypto risk: %.2f%% = $%.2f/trade\n",
               EngineRiskLimits::CRYPTO_RISK_PCT,
               capital_nzd_ * EngineRiskLimits::CRYPTO_RISK_PCT / 100.0);
    }
    
    // =========================================================================
    // PER-ORDER ENFORCEMENT (Call BEFORE every order submission)
    // This is NON-NEGOTIABLE. Put this at the execution boundary.
    // =========================================================================
    [[nodiscard]] bool canSubmitOrder(EngineId engine) {
        // Check 1: Daily loss guard (HARD STOP)
        if (!daily_loss_ || !daily_loss_->allow()) {
            triggerShutdown(ShutdownReason::DAILY_LOSS_LIMIT);
            return false;
        }
        
        // Check 2: Kill switch
        if (kill_switch_ && kill_switch_->killed()) {
            return false;
        }
        
        // Check 3: Already shut down
        if (shutdown_reason_ != ShutdownReason::NONE) {
            return false;
        }
        
        // Check 4: Aggression state
        AggressionState agg = aggression_state_.load();
        if (agg == AggressionState::PROTECTION) {
            // Protection day - no new entries
            printf("[RISK-GOVERNOR] BLOCKED: %s (PROTECTION day)\n", engine_id_str(engine));
            return false;
        }
        
        if (agg == AggressionState::REDUCED && engine == EngineId::BINANCE) {
            // Reduced mode - crypto disabled
            printf("[RISK-GOVERNOR] BLOCKED: CRYPTO (REDUCED mode)\n");
            return false;
        }
        
        // Check 5: Trade count limits
        int* count = getTradeCount(engine);
        if (count && *count >= EngineRiskLimits::getMaxTrades(engine)) {
            printf("[RISK-GOVERNOR] BLOCKED: %s (max trades reached: %d)\n",
                   engine_id_str(engine), *count);
            return false;
        }
        
        // Check 6: Drawdown throttle - block new entries when DD > 75%
        double dd_used = daily_loss_->drawdown_used();
        if (dd_used > 0.75) {
            printf("[RISK-GOVERNOR] BLOCKED: %s (DD_used=%.1f%% > 75%%)\n",
                   engine_id_str(engine), dd_used * 100.0);
            return false;
        }
        
        return true;
    }
    
    // =========================================================================
    // SIZE MULTIPLIER (For position sizing)
    // =========================================================================
    [[nodiscard]] double sizeMultiplier(EngineId engine) const {
        if (!daily_loss_) return 0.0;
        
        // Base throttle from drawdown curve
        double Q_dd = daily_loss_->throttle_factor(2.0);  // Aggressive slowdown
        
        // Aggression state modifier
        double agg_mult = 1.0;
        AggressionState agg = aggression_state_.load();
        if (agg == AggressionState::REDUCED) {
            if (engine == EngineId::CFD) agg_mult = 0.5;       // CFD at 50%
            if (engine == EngineId::BINANCE) agg_mult = 0.0;   // Crypto disabled
        } else if (agg == AggressionState::PROTECTION) {
            agg_mult = 0.0;  // No trading
        }
        
        return Q_dd * agg_mult;
    }
    
    // =========================================================================
    // RISK AMOUNT (NZD per trade for an engine)
    // =========================================================================
    [[nodiscard]] double maxRiskNZD(EngineId engine) const {
        double base_risk = capital_nzd_ * EngineRiskLimits::getRiskPct(engine) / 100.0;
        return base_risk * sizeMultiplier(engine);
    }
    
    // =========================================================================
    // OUTCOME REPORTING (Call after each trade completes)
    // =========================================================================
    void onTradeComplete(EngineId engine, double pnl_nzd) {
        // Update trade counts
        int* count = getTradeCount(engine);
        if (count) (*count)++;
        
        // Track consecutive losses
        if (pnl_nzd < -1.0) {  // Loss (more than scratch)
            consecutive_losses_++;
            last_loss_engine_ = engine;
            
            // Check for two consecutive losses across engines
            if (consecutive_losses_ >= 2) {
                printf("[RISK-GOVERNOR] Two consecutive losses detected!\n");
                triggerShutdown(ShutdownReason::CONSECUTIVE_LOSSES);
            }
            
            // Crypto: Kill on first loss
            if (engine == EngineId::BINANCE) {
                printf("[RISK-GOVERNOR] Crypto first loss - disabling crypto\n");
                crypto_killed_ = true;
            }
            
            // Income loss: Enter protection mode
            if (engine == EngineId::INCOME) {
                printf("[RISK-GOVERNOR] Income engine LOSS - entering PROTECTION mode\n");
                aggression_state_.store(AggressionState::PROTECTION);
            }
        } else if (pnl_nzd > 1.0) {  // Win
            // Reset consecutive loss counter on win
            consecutive_losses_ = 0;
            
            // Income win: Full aggression
            if (engine == EngineId::INCOME) {
                printf("[RISK-GOVERNOR] Income engine WIN - FULL aggression enabled\n");
                aggression_state_.store(AggressionState::FULL);
            }
        } else {
            // Scratch
            if (engine == EngineId::INCOME) {
                printf("[RISK-GOVERNOR] Income engine SCRATCH - REDUCED aggression\n");
                aggression_state_.store(AggressionState::REDUCED);
            }
        }
    }
    
    // =========================================================================
    // SHUTDOWN
    // =========================================================================
    void triggerShutdown(ShutdownReason reason) {
        if (shutdown_reason_ != ShutdownReason::NONE) return;  // Already shut down
        
        shutdown_reason_ = reason;
        shutdown_ts_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        printf("[RISK-GOVERNOR] ══════════════════════════════════════════════════\n");
        printf("[RISK-GOVERNOR] SHUTDOWN TRIGGERED: %s\n", shutdown_reason_str(reason));
        printf("[RISK-GOVERNOR] Daily PnL: $%.2f NZD\n", daily_loss_ ? daily_loss_->pnl() : 0.0);
        printf("[RISK-GOVERNOR] ══════════════════════════════════════════════════\n");
        
        // Kill all engines
        if (kill_switch_) {
            kill_switch_->kill();
        }
    }
    
    void panicShutdown() {
        triggerShutdown(ShutdownReason::MANUAL_PANIC);
    }
    
    // =========================================================================
    // DAILY RESET
    // =========================================================================
    void resetDaily() {
        income_trades_ = 0;
        cfd_trades_ = 0;
        crypto_trades_ = 0;
        consecutive_losses_ = 0;
        crypto_killed_ = false;
        shutdown_reason_ = ShutdownReason::NONE;
        shutdown_ts_ns_ = 0;
        aggression_state_.store(AggressionState::FULL);
        
        printf("[RISK-GOVERNOR] Daily state reset - ready for new session\n");
    }
    
    // =========================================================================
    // STATUS / DIAGNOSTICS
    // =========================================================================
    [[nodiscard]] bool isShutdown() const { return shutdown_reason_ != ShutdownReason::NONE; }
    [[nodiscard]] ShutdownReason shutdownReason() const { return shutdown_reason_; }
    [[nodiscard]] AggressionState aggressionState() const { return aggression_state_.load(); }
    [[nodiscard]] bool isCryptoKilled() const { return crypto_killed_; }
    [[nodiscard]] int consecutiveLosses() const { return consecutive_losses_; }
    
    [[nodiscard]] double drawdownUsed() const {
        return daily_loss_ ? daily_loss_->drawdown_used() : 0.0;
    }
    
    [[nodiscard]] double throttleFactor() const {
        return daily_loss_ ? daily_loss_->throttle_factor(2.0) : 0.0;
    }
    
    void printStatus() const {
        printf("[RISK-GOVERNOR] Status:\n");
        printf("  Daily PnL: $%.2f NZD (limit: $%.0f)\n",
               daily_loss_ ? daily_loss_->pnl() : 0.0,
               daily_loss_ ? daily_loss_->limit() : 0.0);
        printf("  DD used: %.1f%%, Throttle: %.2f\n",
               drawdownUsed() * 100.0, throttleFactor());
        printf("  Aggression: %s\n", aggression_str(aggression_state_.load()));
        printf("  Trades: Income=%d/%d, CFD=%d/%d, Crypto=%d/%d%s\n",
               income_trades_, EngineRiskLimits::INCOME_MAX_TRADES,
               cfd_trades_, EngineRiskLimits::CFD_MAX_TRADES,
               crypto_trades_, EngineRiskLimits::CRYPTO_MAX_TRADES,
               crypto_killed_ ? " [KILLED]" : "");
        printf("  Consecutive losses: %d\n", consecutive_losses_);
        if (shutdown_reason_ != ShutdownReason::NONE) {
            printf("  SHUTDOWN: %s\n", shutdown_reason_str(shutdown_reason_));
        }
    }
    
    // For GUI/dashboard JSON
    void toJSON(char* buf, size_t bufsize) const {
        snprintf(buf, bufsize,
            "{\"daily_pnl\":%.2f,\"daily_limit\":%.0f,\"dd_used\":%.3f,\"throttle\":%.3f,"
            "\"aggression\":\"%s\",\"income_trades\":%d,\"cfd_trades\":%d,\"crypto_trades\":%d,"
            "\"crypto_killed\":%s,\"consecutive_losses\":%d,\"shutdown\":\"%s\"}",
            daily_loss_ ? daily_loss_->pnl() : 0.0,
            daily_loss_ ? daily_loss_->limit() : 0.0,
            drawdownUsed(),
            throttleFactor(),
            aggression_str(aggression_state_.load()),
            income_trades_, cfd_trades_, crypto_trades_,
            crypto_killed_ ? "true" : "false",
            consecutive_losses_,
            shutdown_reason_str(shutdown_reason_));
    }
    
private:
    GlobalRiskGovernor() = default;
    
    int* getTradeCount(EngineId engine) {
        switch (engine) {
            case EngineId::INCOME:  return &income_trades_;
            case EngineId::CFD:     return &cfd_trades_;
            case EngineId::BINANCE: return &crypto_trades_;
            default: return nullptr;
        }
    }
    
    // External references (set via init())
    DailyLossGuard* daily_loss_ = nullptr;
    GlobalKill* kill_switch_ = nullptr;
    double capital_nzd_ = 15000.0;  // Default
    
    // Trade counts (per day)
    int income_trades_ = 0;
    int cfd_trades_ = 0;
    int crypto_trades_ = 0;
    
    // Loss tracking
    int consecutive_losses_ = 0;
    EngineId last_loss_engine_ = EngineId::UNKNOWN;
    bool crypto_killed_ = false;
    
    // Aggression state
    std::atomic<AggressionState> aggression_state_{AggressionState::FULL};
    
    // Shutdown state
    ShutdownReason shutdown_reason_ = ShutdownReason::NONE;
    uint64_t shutdown_ts_ns_ = 0;
};

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

inline bool canSubmitOrder(EngineId engine) {
    return GlobalRiskGovernor::instance().canSubmitOrder(engine);
}

inline double sizeMultiplier(EngineId engine) {
    return GlobalRiskGovernor::instance().sizeMultiplier(engine);
}

inline double maxRiskNZD(EngineId engine) {
    return GlobalRiskGovernor::instance().maxRiskNZD(engine);
}

inline void onTradeComplete(EngineId engine, double pnl_nzd) {
    GlobalRiskGovernor::instance().onTradeComplete(engine, pnl_nzd);
}

inline void panicShutdown() {
    GlobalRiskGovernor::instance().panicShutdown();
}

} // namespace Chimera
