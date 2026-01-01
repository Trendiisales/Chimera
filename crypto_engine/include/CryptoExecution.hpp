#pragma once
// =============================================================================
// CRYPTO EXECUTION - Order Submission Interface
// =============================================================================
// Hard execution boundary - ownership/risk must already be satisfied upstream.
// This only handles the actual order submission.
// v4.5.1: Now enforces GlobalRiskGovernor at execution boundary
// =============================================================================

#include <string>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <functional>

// v4.5.1: Include for execution-layer enforcement
#include "core/EngineOwnership.hpp"
#include "shared/GlobalRiskGovernor.hpp"

namespace Chimera::Crypto {

// =============================================================================
// Execution Callback Types
// =============================================================================
using OrderCallback = std::function<void(const char* symbol, const char* side,
                                         double size, double price)>;
using FillCallback = std::function<void(const char* symbol, double fill_price,
                                        double fill_size, double pnl_usd)>;

// =============================================================================
// Crypto Execution Handler
// =============================================================================
class CryptoExecution {
private:
    static OrderCallback order_callback_;
    static bool live_mode_;
    
public:
    // ═══════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════
    static void setLiveMode(bool live) noexcept {
        live_mode_ = live;
        std::cout << "[CRYPTO-EXEC] Mode: " << (live ? "LIVE" : "PAPER") << "\n";
    }
    
    static void setOrderCallback(OrderCallback cb) noexcept {
        order_callback_ = std::move(cb);
    }
    
    static bool isLive() noexcept {
        return live_mode_;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Submit Order
    // ═══════════════════════════════════════════════════════════════════
    static bool submitOrder(const char* symbol,
                            const char* side,
                            double size,
                            double stop_px,
                            double target_px,
                            int64_t now_ms) noexcept {
        // =====================================================================
        // v4.5.1 HARD EXECUTION GUARD (NON-NEGOTIABLE)
        // This is at the execution boundary - NOTHING bypasses this
        // =====================================================================
        if (!Chimera::GlobalRiskGovernor::instance().canSubmitOrder(Chimera::EngineId::BINANCE)) {
            std::cerr << "[CRYPTO-EXEC] BLOCKED: RISK GOVERNOR (daily loss or throttle)\n";
            return false;
        }
        
        // Safety assertion - symbol scope
        if (std::string(symbol) != "BTCUSDT" && std::string(symbol) != "ETHUSDT") {
            std::cerr << "[CRYPTO-EXEC] BLOCKED: Invalid symbol " << symbol << "\n";
            return false;
        }
        
        // Safety assertion - side
        if (std::string(side) != "BUY" && std::string(side) != "SELL") {
            std::cerr << "[CRYPTO-EXEC] BLOCKED: Invalid side " << side << "\n";
            return false;
        }
        
        // Safety assertion - size
        if (size <= 0 || size > 0.1) {  // Max 0.1 BTC / 1.0 ETH
            std::cerr << "[CRYPTO-EXEC] BLOCKED: Invalid size " << size << "\n";
            return false;
        }
        
        std::cout << "[CRYPTO-EXEC] " << side
                  << " " << symbol
                  << " size=" << std::fixed << std::setprecision(4) << size
                  << " stop=" << std::setprecision(2) << stop_px
                  << " target=" << target_px
                  << " t=" << now_ms
                  << " mode=" << (live_mode_ ? "LIVE" : "PAPER")
                  << "\n";
        
        if (live_mode_ && order_callback_) {
            // Send to exchange via callback
            double limit_price = 0.0;  // Market order, price determined at fill
            order_callback_(symbol, side, size, limit_price);
        }
        
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // Submit Exit Order
    // ═══════════════════════════════════════════════════════════════════
    static bool submitExit(const char* symbol,
                           const char* side,  // Opposite of entry
                           double size,
                           const char* reason,
                           int64_t now_ms) noexcept {
        std::cout << "[CRYPTO-EXEC] EXIT " << side
                  << " " << symbol
                  << " size=" << std::fixed << std::setprecision(4) << size
                  << " reason=" << reason
                  << " t=" << now_ms
                  << "\n";
        
        if (live_mode_ && order_callback_) {
            order_callback_(symbol, side, size, 0.0);
        }
        
        return true;
    }
};

// Static member definitions
inline OrderCallback CryptoExecution::order_callback_;
inline bool CryptoExecution::live_mode_ = false;

} // namespace Chimera::Crypto
