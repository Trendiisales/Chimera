// =============================================================================
// CfdEngineIntegration.hpp - v4.10.2 - LOCKED INTEGRATION LAYER
// =============================================================================
// HARD LOCKS (v4.10.2):
//   ✅ SYMBOLS: NAS100, US30 ONLY
//   ❌ No US100 alias
//   ❌ No SPX500
//   ❌ No FX (EURUSD, GBPUSD, USDJPY)
//   ❌ No Gold (XAUUSD)
//
// At startup you will see:
//   [SYMBOLS] Registered: NAS100, US30
//
// Nothing else. Two symbols only.
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-06
// =============================================================================
#pragma once

#include "engines/IndexImpulseEngine.hpp"
#include "portfolio/PortfolioModeController.hpp"
#include "quality/MarketQualityCuts.hpp"

#include <cstdint>
#include <cstdio>
#include <chrono>

namespace Chimera {

// =============================================================================
// INTEGRATION OUTPUT
// =============================================================================
struct IntegrationOutput {
    bool should_trade = false;
    int8_t direction = 0;
    double size = 0.0;
    double stop_loss = 0.0;
    const char* engine = "";
    const char* reason = "";
    bool is_exit = false;
    bool is_partial = false;
    double realized_pnl = 0.0;
};

// =============================================================================
// CFD ENGINE INTEGRATION (v4.10.2 - LOCKED)
// =============================================================================
class CfdEngineIntegration {
public:
    CfdEngineIntegration() = default;
    
    // =========================================================================
    // INITIALIZATION (v4.10.2 - NAS100/US30 ONLY)
    // =========================================================================
    void init(double starting_equity) {
        starting_equity_ = starting_equity;
        
        // Initialize portfolio controller
        auto& portfolio = getPortfolioController();
        portfolio.initDay(starting_equity);
        
        // =====================================================================
        // v4.10.2: ONLY NAS100 AND US30
        // =====================================================================
        printf("[SYMBOLS] Registering allowed symbols...\n");
        
        portfolio.registerSymbol("NAS100");
        portfolio.registerSymbol("US30");
        
        // Count what we registered
        int count = 0;
        if (portfolio.isSymbolEnabled("NAS100")) count++;
        if (portfolio.isSymbolEnabled("US30")) count++;
        
        printf("[SYMBOLS] Registered: NAS100, US30\n");
        printf("[SYMBOLS] Total: %d symbols (v4.10.2 lock active)\n", count);
        
        // Verify the lock
        if (count != 2) {
            printf("[SYMBOLS] ERROR: Expected 2 symbols, got %d\n", count);
        }
        
        // =====================================================================
        // REJECTED SYMBOLS (for clarity in logs)
        // =====================================================================
        printf("[SYMBOLS] REJECTED (v4.10.2 lock):\n");
        printf("[SYMBOLS]   - US100 (use NAS100 instead)\n");
        printf("[SYMBOLS]   - SPX500\n");
        printf("[SYMBOLS]   - EURUSD, GBPUSD, USDJPY (FX disabled)\n");
        printf("[SYMBOLS]   - XAUUSD (Gold disabled)\n");
        
        // Configure index engine
        IndexEngineConfig idx_cfg;
        idx_cfg.nas100_risk = 0.005;  // 0.5% fixed
        idx_cfg.us30_risk = 0.004;    // 0.4% fixed
        getIndexImpulseEngine().setConfig(idx_cfg);
        
        initialized_ = true;
        
        printf("[CFD-INTEGRATION] Initialized v4.10.2 (LOCKED)\n");
        printf("[CFD-INTEGRATION] Risk: NAS100=0.50%% US30=0.40%% (FIXED)\n");
    }
    
    // =========================================================================
    // TICK HANDLER
    // =========================================================================
    IntegrationOutput onTick(
        const char* symbol,
        double bid,
        double ask,
        double volume,
        uint64_t now_ns
    ) {
        IntegrationOutput out;
        out.reason = "INIT";
        
        if (!initialized_) {
            out.reason = "NOT_INITIALIZED";
            return out;
        }
        
        auto& portfolio = getPortfolioController();
        
        // Check if halted
        if (portfolio.isHalted()) {
            out.reason = "PORTFOLIO_HALTED";
            return out;
        }
        
        // v4.10.2: Hard reject non-allowed symbols
        if (!isAllowedSymbol(symbol)) {
            out.reason = "SYMBOL_NOT_ALLOWED";
            return out;
        }
        
        // Check if symbol enabled
        if (!portfolio.isSymbolEnabled(symbol)) {
            out.reason = portfolio.getDisableReason(symbol);
            return out;
        }
        
        // Get current equity
        double equity = starting_equity_ + portfolio.getState().daily_pnl;
        
        // Get FIXED risk for logging
        double risk_pct = getFixedRisk(symbol);
        
        // Route to index engine (E2 primary)
        auto& idx_engine = getIndexImpulseEngine();
        auto engine_out = idx_engine.onTick(symbol, bid, ask, volume, now_ns, equity);
        
        // Convert to integration output
        out.should_trade = engine_out.should_trade;
        out.direction = engine_out.direction;
        out.size = engine_out.size;
        out.stop_loss = engine_out.stop_loss;
        out.engine = engine_out.engine;
        out.reason = engine_out.reason;
        out.is_exit = engine_out.is_exit;
        out.is_partial = engine_out.is_partial;
        out.realized_pnl = engine_out.realized_pnl;
        
        // Log risk on order submission
        if (out.should_trade && !out.is_exit) {
            printf("[RISK] %s risk=%.2f%%\n", symbol, risk_pct * 100.0);
        }
        
        return out;
    }
    
    // =========================================================================
    // BAR CLOSE HANDLER
    // =========================================================================
    void onBarClose(const char* symbol) {
        if (!isAllowedSymbol(symbol)) return;
        getIndexImpulseEngine().onBarClose(symbol);
    }
    
    // =========================================================================
    // TRADE RESULT HANDLER
    // =========================================================================
    void onTradeResult(const char* symbol, int8_t direction, double pnl_dollars, double risk_dollars) {
        auto& portfolio = getPortfolioController();
        portfolio.updatePnL(pnl_dollars, risk_dollars);
        
        printf("[CFD-INTEGRATION] Trade result: %s %s pnl=$%.2f daily=$%.2f\n",
               symbol, direction > 0 ? "LONG" : "SHORT", pnl_dollars,
               portfolio.getState().daily_pnl);
    }
    
    // =========================================================================
    // DAILY RESET
    // =========================================================================
    void resetDaily(double equity) {
        starting_equity_ = equity;
        
        auto& portfolio = getPortfolioController();
        portfolio.initDay(equity);
        
        // Re-register only allowed symbols
        portfolio.registerSymbol("NAS100");
        portfolio.registerSymbol("US30");
        
        getIndexImpulseEngine().resetDaily();
        getMarketQualityCuts().resetDaily();
        
        printf("[CFD-INTEGRATION] Daily reset, equity=$%.2f\n", equity);
        printf("[SYMBOLS] Registered: NAS100, US30\n");
    }
    
    // =========================================================================
    // GETTERS
    // =========================================================================
    [[nodiscard]] bool isInitialized() const { return initialized_; }
    [[nodiscard]] PortfolioMode getMode() const { return getPortfolioController().mode(); }
    
    // =========================================================================
    // STATUS
    // =========================================================================
    void printStatus() const {
        printf("\n═══════════════════════════════════════════════════════════════\n");
        printf("CFD ENGINE INTEGRATION STATUS (v4.10.2 LOCKED)\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        
        printf("SYMBOL LOCK:\n");
        printf("  ✅ NAS100: ENABLED (0.50%% fixed)\n");
        printf("  ✅ US30:   ENABLED (0.40%% fixed)\n");
        printf("  ❌ US100:  REJECTED (use NAS100)\n");
        printf("  ❌ SPX500: REJECTED\n");
        printf("  ❌ FX:     REJECTED\n");
        printf("  ❌ Gold:   REJECTED\n");
        printf("\n");
        
        getPortfolioController().printStatus();
        printf("\n");
        
        getIndexImpulseEngine().printStatus();
        printf("═══════════════════════════════════════════════════════════════\n\n");
    }

private:
    bool initialized_ = false;
    double starting_equity_ = 100000.0;
};

// =============================================================================
// GLOBAL CFD ENGINE INTEGRATION ACCESSOR
// =============================================================================
inline CfdEngineIntegration& getCfdEngineIntegration() {
    static CfdEngineIntegration instance;
    return instance;
}

} // namespace Chimera
