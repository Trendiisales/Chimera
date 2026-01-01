// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/SlippageGovernor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Promote slippage from metric to governor
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Closes the execution risk loop
//
// PRINCIPLE: "Slippage is a signal, not just a cost"
// - Rolling realized slippage tracking
// - Compare vs expected slippage
// - Automatic size/mode adjustments
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <array>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Slippage State
// ─────────────────────────────────────────────────────────────────────────────
enum class SlippageState : uint8_t {
    NORMAL,      // Slippage within expected range
    ELEVATED,    // +30% above expected → halve size
    HIGH,        // +60% above expected → taker only
    CRITICAL     // +100% above expected → pause symbol
};

inline const char* slippage_state_str(SlippageState s) noexcept {
    switch (s) {
        case SlippageState::NORMAL:   return "NORMAL";
        case SlippageState::ELEVATED: return "ELEVATED";
        case SlippageState::HIGH:     return "HIGH";
        case SlippageState::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Slippage Tracker
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolSlippage {
    double expected_slippage_bps = 0.5;   // Baseline expected slippage
    double realized_slippage_bps = 0.0;   // EWMA of actual slippage
    int fill_count = 0;
    SlippageState state = SlippageState::NORMAL;
    
    static constexpr double ALPHA = 0.1;  // EWMA smoothing
    
    void record_fill(double expected_price, double fill_price, bool is_buy) noexcept {
        fill_count++;
        
        // Calculate slippage in bps (positive = worse than expected)
        double slippage;
        if (is_buy) {
            slippage = (fill_price - expected_price) / expected_price * 10000.0;
        } else {
            slippage = (expected_price - fill_price) / expected_price * 10000.0;
        }
        
        // EWMA update
        realized_slippage_bps = ALPHA * slippage + (1.0 - ALPHA) * realized_slippage_bps;
        
        // Update state
        update_state();
    }
    
    void update_state() noexcept {
        if (expected_slippage_bps <= 0) {
            state = SlippageState::NORMAL;
            return;
        }
        
        double ratio = realized_slippage_bps / expected_slippage_bps;
        
        if (ratio >= 2.0) {
            state = SlippageState::CRITICAL;
        } else if (ratio >= 1.6) {
            state = SlippageState::HIGH;
        } else if (ratio >= 1.3) {
            state = SlippageState::ELEVATED;
        } else {
            state = SlippageState::NORMAL;
        }
    }
    
    [[nodiscard]] double size_multiplier() const noexcept {
        switch (state) {
            case SlippageState::NORMAL:   return 1.0;
            case SlippageState::ELEVATED: return 0.5;
            case SlippageState::HIGH:     return 0.25;
            case SlippageState::CRITICAL: return 0.0;
            default: return 0.0;
        }
    }
    
    [[nodiscard]] bool maker_only() const noexcept {
        return state >= SlippageState::HIGH;
    }
    
    [[nodiscard]] bool paused() const noexcept {
        return state == SlippageState::CRITICAL;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Slippage Governor (multi-symbol)
// ─────────────────────────────────────────────────────────────────────────────
class SlippageGovernor {
public:
    static constexpr size_t MAX_SYMBOLS = 32;
    
    struct Config {
        double elevated_threshold;   // +30%
        double high_threshold;       // +60%
        double critical_threshold;   // +100%
        
        Config()
            : elevated_threshold(1.3)
            , high_threshold(1.6)
            , critical_threshold(2.0)
        {}
    };
    
    explicit SlippageGovernor(const Config& cfg = Config()) : cfg_(cfg) {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Set expected slippage for a symbol
    // ─────────────────────────────────────────────────────────────────────────
    void set_expected(uint16_t symbol_id, double expected_bps) noexcept {
        if (symbol_id < MAX_SYMBOLS) {
            symbols_[symbol_id].expected_slippage_bps = expected_bps;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record a fill
    // ─────────────────────────────────────────────────────────────────────────
    void record_fill(uint16_t symbol_id, double expected_price, 
                     double fill_price, bool is_buy) noexcept {
        if (symbol_id >= MAX_SYMBOLS) return;
        
        SymbolSlippage& s = symbols_[symbol_id];
        SlippageState old_state = s.state;
        
        s.record_fill(expected_price, fill_price, is_buy);
        
        // Log state changes
        if (s.state != old_state) {
            std::cout << "[SLIPPAGE-" << symbol_id << "] "
                      << slippage_state_str(old_state) << " → " << slippage_state_str(s.state)
                      << " | realized=" << s.realized_slippage_bps << "bps"
                      << " expected=" << s.expected_slippage_bps << "bps"
                      << " ratio=" << (s.realized_slippage_bps / s.expected_slippage_bps)
                      << "\n";
                      
            if (s.state == SlippageState::CRITICAL) {
                std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
                std::cout << "║  🔴 SLIPPAGE CRITICAL - Symbol " << symbol_id << " PAUSED\n";
                std::cout << "║  Realized: " << s.realized_slippage_bps << " bps\n";
                std::cout << "║  Expected: " << s.expected_slippage_bps << " bps\n";
                std::cout << "║  Ratio: " << (s.realized_slippage_bps / s.expected_slippage_bps) << "x\n";
                std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            }
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Query state
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] SlippageState state(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return SlippageState::NORMAL;
        return symbols_[symbol_id].state;
    }
    
    [[nodiscard]] double size_multiplier(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return 1.0;
        return symbols_[symbol_id].size_multiplier();
    }
    
    [[nodiscard]] bool maker_only(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return false;
        return symbols_[symbol_id].maker_only();
    }
    
    [[nodiscard]] bool paused(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return false;
        return symbols_[symbol_id].paused();
    }
    
    [[nodiscard]] double realized_slippage(uint16_t symbol_id) const noexcept {
        if (symbol_id >= MAX_SYMBOLS) return 0.0;
        return symbols_[symbol_id].realized_slippage_bps;
    }

private:
    Config cfg_;
    std::array<SymbolSlippage, MAX_SYMBOLS> symbols_{};
};

} // namespace Risk
} // namespace Chimera
