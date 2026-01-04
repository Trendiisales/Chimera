#pragma once
// =============================================================================
// CRYPTO ENGINE - Main Controller
// =============================================================================
// Modes:
//   STUB         - No execution, logging only (default)
//   OPPORTUNISTIC - Live execution when all gates pass
//
// This engine NEVER interferes with IncomeEngine or CFDEngine.
// =============================================================================

#include <string>
#include <functional>
#include <iostream>
#include <iomanip>
#include <atomic>

#include "CryptoSignalEvaluator.hpp"
#include "CryptoRiskManager.hpp"
#include "CryptoExecution.hpp"

namespace Chimera::Crypto {

// =============================================================================
// Engine Mode
// =============================================================================
enum class CryptoMode {
    STUB,           // No execution (safe default)
    OPPORTUNISTIC   // Execute when conditions are exceptional
};

inline const char* modeStr(CryptoMode m) {
    switch (m) {
        case CryptoMode::STUB: return "STUB";
        case CryptoMode::OPPORTUNISTIC: return "OPPORTUNISTIC";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Cross-Engine Check Callbacks
// =============================================================================
using HasPositionCallback = std::function<bool()>;  // Returns true if position active

// =============================================================================
// Engine State for GUI
// =============================================================================
enum class CryptoState {
    DISABLED,       // Killed or blocked
    IDLE,           // No conditions met
    NEAR,           // Conditions forming
    ARMED,          // Ready to trade (waiting acceptance)
    TRADING,        // Position open
    COOLDOWN        // After trade, waiting for reset
};

inline const char* stateStr(CryptoState s) {
    switch (s) {
        case CryptoState::DISABLED: return "DISABLED";
        case CryptoState::IDLE: return "IDLE";
        case CryptoState::NEAR: return "NEAR";
        case CryptoState::ARMED: return "ARMED";
        case CryptoState::TRADING: return "TRADING";
        case CryptoState::COOLDOWN: return "COOLDOWN";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// RTT Constants
// =============================================================================
namespace EngineConstants {
    constexpr double MAX_RTT_MS = 1.2;
}

// =============================================================================
// Main Crypto Engine
// =============================================================================
class CryptoEngine {
private:
    CryptoMode mode_;
    CryptoSignalEvaluator evaluator_;
    std::atomic<CryptoState> state_{CryptoState::IDLE};
    
    // Cross-engine position checks
    HasPositionCallback income_has_position_;
    HasPositionCallback cfd_has_position_;
    
    // Tick counter for periodic logging
    uint64_t tick_count_ = 0;
    
    // Block reason for GUI
    const char* block_reason_ = "";
    
    // Last signal for GUI
    CryptoSignal last_signal_;

public:
    // ═══════════════════════════════════════════════════════════════════
    // Construction
    // ═══════════════════════════════════════════════════════════════════
    explicit CryptoEngine(CryptoMode mode = CryptoMode::STUB)
        : mode_(mode) {
        std::cout << "[CRYPTO-ENGINE] Initialized in " << modeStr(mode) << " mode\n";
        std::cout << "[CRYPTO-ENGINE] Symbols: BTCUSDT, ETHUSDT ONLY\n";
        std::cout << "[CRYPTO-ENGINE] Max trades/day: " << RiskConstants::MAX_TRADES_PER_DAY << "\n";
        std::cout << "[CRYPTO-ENGINE] Risk per trade: " << RiskConstants::RISK_PER_TRADE_PCT << "%\n";
        std::cout << "[CRYPTO-ENGINE] Kill on first loss: YES\n";
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════
    void setMode(CryptoMode mode) noexcept {
        mode_ = mode;
        std::cout << "[CRYPTO-ENGINE] Mode changed to " << modeStr(mode) << "\n";
    }
    
    CryptoMode mode() const noexcept { return mode_; }
    
    void setIncomePositionCallback(HasPositionCallback cb) noexcept {
        income_has_position_ = std::move(cb);
    }
    
    void setCFDPositionCallback(HasPositionCallback cb) noexcept {
        cfd_has_position_ = std::move(cb);
    }
    
    void setEquity(double equity) noexcept {
        CryptoRiskManager::instance().setEquity(equity);
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // State Query (for GUI)
    // ═══════════════════════════════════════════════════════════════════
    CryptoState state() const noexcept { return state_.load(); }
    const char* blockReason() const noexcept { return block_reason_; }
    const CryptoSignal& lastSignal() const noexcept { return last_signal_; }
    
    // ═══════════════════════════════════════════════════════════════════
    // Main Tick Handler
    // ═══════════════════════════════════════════════════════════════════
    void onTick(const char* symbol,
                double price,
                double spread,
                double /*vwap*/,          // Can be 0 if not available
                double bid_vol_5,
                double ask_vol_5,
                double rtt_ms,
                int64_t now_ms) noexcept {
        
        tick_count_++;
        auto& risk = CryptoRiskManager::instance();
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 0: Symbol Scope (HARD)
        // ═══════════════════════════════════════════════════════════════
        if (std::string(symbol) != "BTCUSDT" && std::string(symbol) != "ETHUSDT") {
            return;  // Silent ignore - not our symbols
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 1: Infrastructure (RTT)
        // ═══════════════════════════════════════════════════════════════
        if (rtt_ms > EngineConstants::MAX_RTT_MS) {
            block_reason_ = "RTT_HIGH";
            state_ = CryptoState::DISABLED;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 2: Kill Switch Check
        // ═══════════════════════════════════════════════════════════════
        if (risk.isKilled()) {
            block_reason_ = killReasonStr(risk.killReason());
            state_ = CryptoState::DISABLED;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // Update market data for evaluator
        // ═══════════════════════════════════════════════════════════════
        double volume = (bid_vol_5 + ask_vol_5) / 2.0;  // Approximate
        evaluator_.onTick(symbol, price, spread, volume, now_ms);
        
        // ═══════════════════════════════════════════════════════════════
        // Position Management (if holding)
        // ═══════════════════════════════════════════════════════════════
        if (risk.hasPosition()) {
            state_ = CryptoState::TRADING;
            const auto& pos = risk.position();
            
            // Check exit conditions
            bool should_exit = evaluator_.shouldExit(
                symbol, price, pos.entry_spread, spread,
                bid_vol_5, ask_vol_5, pos.is_long,
                pos.entry_time_ms, now_ms
            );
            
            // Check stop/target
            if (pos.is_long) {
                if (price <= pos.stop_px) {
                    exitPosition(price, now_ms, "STOP");
                    return;
                }
                if (price >= pos.target_px) {
                    exitPosition(price, now_ms, "TARGET");
                    return;
                }
            } else {
                if (price >= pos.stop_px) {
                    exitPosition(price, now_ms, "STOP");
                    return;
                }
                if (price <= pos.target_px) {
                    exitPosition(price, now_ms, "TARGET");
                    return;
                }
            }
            
            // Check timeout
            if (risk.isPositionTimedOut(now_ms)) {
                exitPosition(price, now_ms, "TIMEOUT");
                return;
            }
            
            // Check signal-based exit
            if (should_exit) {
                exitPosition(price, now_ms, "SIGNAL_EXIT");
                return;
            }
            
            return;  // Still holding
        }
        
        // ═══════════════════════════════════════════════════════════════
        // GATE 3: Cross-Engine Position Check
        // ═══════════════════════════════════════════════════════════════
        if (income_has_position_ && income_has_position_()) {
            block_reason_ = "INCOME_ACTIVE";
            state_ = CryptoState::DISABLED;
            return;
        }
        
        if (cfd_has_position_ && cfd_has_position_()) {
            block_reason_ = "CFD_ACTIVE";
            state_ = CryptoState::DISABLED;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // Evaluate Signal
        // ═══════════════════════════════════════════════════════════════
        CryptoSignal sig = evaluator_.evaluate(
            symbol, price, spread, bid_vol_5, ask_vol_5, now_ms
        );
        last_signal_ = sig;
        
        // Update state for GUI
        if (sig.ready_to_trade) {
            state_ = CryptoState::ARMED;
        } else if (sig.near_trigger) {
            state_ = CryptoState::NEAR;
            block_reason_ = sig.block_reason;
        } else {
            state_ = CryptoState::IDLE;
            block_reason_ = sig.block_reason;
        }
        
        // Log near trigger
        if (sig.near_trigger && tick_count_ % 100 == 0) {
            std::cout << "[CRYPTO-ENGINE] NEAR | " << symbol
                      << " imb=" << std::fixed << std::setprecision(2) << sig.imbalance
                      << " spread=" << std::setprecision(4) << spread
                      << " block=" << sig.block_reason
                      << "\n";
        }
        
        // ═══════════════════════════════════════════════════════════════
        // Entry Decision
        // ═══════════════════════════════════════════════════════════════
        if (!sig.ready_to_trade) {
            return;
        }
        
        // Mode check
        if (mode_ != CryptoMode::OPPORTUNISTIC) {
            if (tick_count_ % 500 == 0) {
                std::cout << "[CRYPTO-ENGINE] SIGNAL READY but STUBBED | " << symbol
                          << " side=" << sig.side
                          << " imb=" << sig.imbalance
                          << "\n";
            }
            return;
        }
        
        // Risk check
        if (!risk.canTrade()) {
            block_reason_ = "RISK_BLOCK";
            state_ = CryptoState::DISABLED;
            std::cout << "[CRYPTO-ENGINE] BLOCKED by risk manager\n";
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════
        // EXECUTE ENTRY
        // ═══════════════════════════════════════════════════════════════
        double size = risk.fixedSize(symbol);
        bool is_long = (sig.side == "BUY");
        
        // Submit order
        if (CryptoExecution::submitOrder(symbol, sig.side.c_str(), size,
                                         sig.stop_px, sig.target_px, now_ms)) {
            // Record position
            risk.openPosition(symbol, is_long, price, size,
                             sig.stop_px, sig.target_px, sig.entry_spread, now_ms);
            state_ = CryptoState::TRADING;
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Exit Position
    // ═══════════════════════════════════════════════════════════════════
    void exitPosition(double price, int64_t now_ms, const char* reason) noexcept {
        auto& risk = CryptoRiskManager::instance();
        if (!risk.hasPosition()) return;
        
        const auto& pos = risk.position();
        const char* exit_side = pos.is_long ? "SELL" : "BUY";
        
        CryptoExecution::submitExit(pos.symbol, exit_side, pos.size, reason, now_ms);
        (void)risk.closePosition(price, now_ms, reason);
        
        state_ = CryptoState::COOLDOWN;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Manual Controls
    // ═══════════════════════════════════════════════════════════════════
    void emergencyExit(double price, int64_t now_ms) noexcept {
        exitPosition(price, now_ms, "EMERGENCY");
        CryptoRiskManager::instance().killManual();
    }
    
    void kill() noexcept {
        CryptoRiskManager::instance().killManual();
        state_ = CryptoState::DISABLED;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Status
    // ═══════════════════════════════════════════════════════════════════
    void printStatus() const {
        auto& risk = CryptoRiskManager::instance();
        std::cout << "[CRYPTO-ENGINE] mode=" << modeStr(mode_)
                  << " state=" << stateStr(state_.load())
                  << " block=" << block_reason_
                  << "\n";
        risk.printStatus();
    }
};

} // namespace Chimera::Crypto
