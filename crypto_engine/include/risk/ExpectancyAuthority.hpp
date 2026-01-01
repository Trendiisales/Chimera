// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/ExpectancyAuthority.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Dual-horizon expectancy - fast protects, slow decides
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.14: NEW FILE - Prevents false deaths from statistical noise
//
// INVARIANT: "Fast signals protect, slow signals decide"
// - Fast horizon (20-30 trades): Can reduce size, pause entries
// - Slow horizon (100-300 trades): Has authority to disable
// - Fast noise cannot kill slow edge
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <string>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Single-Horizon Expectancy Tracker (internal use)
// ─────────────────────────────────────────────────────────────────────────────
struct ExpectancyHorizon {
    double expectancy_bps = 0.0;
    double avg_win_bps = 0.0;
    double avg_loss_bps = 0.0;
    int trades = 0;
    int wins = 0;
    int losses = 0;
    double alpha = 0.0;  // EWMA smoothing factor
    
    explicit ExpectancyHorizon(int window_size) 
        : alpha(2.0 / (window_size + 1)) 
    {}
    
    void record(double pnl_bps) noexcept {
        trades++;
        
        // EWMA update of expectancy
        expectancy_bps = alpha * pnl_bps + (1.0 - alpha) * expectancy_bps;
        
        // Track win/loss stats
        if (pnl_bps > 0.05) {  // Small threshold to avoid counting scratches as wins
            wins++;
            avg_win_bps = alpha * pnl_bps + (1.0 - alpha) * avg_win_bps;
        } else if (pnl_bps < -0.05) {
            losses++;
            avg_loss_bps = alpha * std::abs(pnl_bps) + (1.0 - alpha) * avg_loss_bps;
        }
    }
    
    [[nodiscard]] double win_rate() const noexcept {
        return trades > 0 ? (100.0 * wins / trades) : 50.0;
    }
    
    [[nodiscard]] bool has_enough_data(int min_trades) const noexcept {
        return trades >= min_trades;
    }
    
    void reset() noexcept {
        expectancy_bps = 0.0;
        avg_win_bps = avg_loss_bps = 0.0;
        trades = wins = losses = 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Dual-Horizon Expectancy Authority
// ─────────────────────────────────────────────────────────────────────────────
class ExpectancyAuthority {
public:
    // Config
    struct Config {
        int fast_window;
        int slow_window;
        int fast_min_trades;
        int slow_min_trades;
        double pause_threshold;
        double disable_threshold;
        double reenable_threshold;
        
        Config() 
            : fast_window(25)
            , slow_window(150)
            , fast_min_trades(15)
            , slow_min_trades(50)
            , pause_threshold(-0.1)
            , disable_threshold(0.0)
            , reenable_threshold(0.2)
        {}
    };
    
    explicit ExpectancyAuthority(const Config& cfg = Config())
        : cfg_(cfg)
        , fast_(cfg.fast_window)
        , slow_(cfg.slow_window)
    {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Record a trade result
    // ─────────────────────────────────────────────────────────────────────────
    void record(double pnl_bps) noexcept {
        fast_.record(pnl_bps);
        slow_.record(pnl_bps);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // AUTHORITY DECISION - What should we do?
    // ─────────────────────────────────────────────────────────────────────────
    enum class Decision {
        FULL_SIZE,      // All systems go
        REDUCED_SIZE,   // Fast is weak, reduce exposure
        PAUSED,         // Fast is bad, no new entries
        DISABLED        // Slow is bad, symbol disabled
    };
    
    // BOOTSTRAP: Number of trades before guards activate
    static constexpr int BOOTSTRAP_TRADES = 20;
    
    [[nodiscard]] Decision decide() const noexcept {
        // ═══════════════════════════════════════════════════════════════════
        // BOOTSTRAP BYPASS (CRITICAL) - Allow system to form expectancy
        // Without this, zero trades forever is guaranteed
        // ═══════════════════════════════════════════════════════════════════
        int total_trades = fast_.trades;
        if (total_trades < BOOTSTRAP_TRADES) {
            return Decision::FULL_SIZE;
        }
        
        // Rule 1: Slow has ultimate authority to disable
        if (slow_.has_enough_data(cfg_.slow_min_trades) && 
            slow_.expectancy_bps < cfg_.disable_threshold) {
            return Decision::DISABLED;
        }
        
        // Rule 2: Fast can pause entries (but not disable)
        if (fast_.has_enough_data(cfg_.fast_min_trades) &&
            fast_.expectancy_bps < cfg_.pause_threshold) {
            return Decision::PAUSED;
        }
        
        // Rule 3: Fast weak + Slow OK = reduced size
        if (fast_.has_enough_data(cfg_.fast_min_trades) &&
            fast_.expectancy_bps < 0.0 &&
            slow_.expectancy_bps > 0.0) {
            return Decision::REDUCED_SIZE;
        }
        
        return Decision::FULL_SIZE;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // SIZE MULTIPLIER - How much to scale position
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double size_multiplier() const noexcept {
        Decision d = decide();
        switch (d) {
            case Decision::DISABLED:     return 0.0;
            case Decision::PAUSED:       return 0.0;
            case Decision::REDUCED_SIZE: return 0.5;
            case Decision::FULL_SIZE:    return expectancy_scalar();
            default: return 0.0;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Can symbol be re-enabled?
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool can_reenable() const noexcept {
        return slow_.has_enough_data(cfg_.slow_min_trades) &&
               slow_.expectancy_bps >= cfg_.reenable_threshold;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Getters
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double fast_expectancy() const noexcept { return fast_.expectancy_bps; }
    [[nodiscard]] double slow_expectancy() const noexcept { return slow_.expectancy_bps; }
    [[nodiscard]] int fast_trades() const noexcept { return fast_.trades; }
    [[nodiscard]] int slow_trades() const noexcept { return slow_.trades; }
    [[nodiscard]] double fast_win_rate() const noexcept { return fast_.win_rate(); }
    [[nodiscard]] double slow_win_rate() const noexcept { return slow_.win_rate(); }
    
    // Authority expectancy = minimum of fast and slow (conservative)
    [[nodiscard]] double authority_expectancy() const noexcept {
        if (!fast_.has_enough_data(cfg_.fast_min_trades)) return slow_.expectancy_bps;
        if (!slow_.has_enough_data(cfg_.slow_min_trades)) return fast_.expectancy_bps;
        return std::min(fast_.expectancy_bps, slow_.expectancy_bps);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Debug output
    // ─────────────────────────────────────────────────────────────────────────
    void print_status(const std::string& symbol) const {
        const char* decision_str = "";
        switch (decide()) {
            case Decision::FULL_SIZE:    decision_str = "FULL"; break;
            case Decision::REDUCED_SIZE: decision_str = "REDUCED"; break;
            case Decision::PAUSED:       decision_str = "PAUSED"; break;
            case Decision::DISABLED:     decision_str = "DISABLED"; break;
        }
        
        std::cout << "[AUTHORITY-" << symbol << "] "
                  << "fast=" << fast_.expectancy_bps << "bps(" << fast_.trades << "t) "
                  << "slow=" << slow_.expectancy_bps << "bps(" << slow_.trades << "t) "
                  << "decision=" << decision_str
                  << " mult=" << size_multiplier() << "x\n";
    }
    
    void reset() noexcept {
        fast_.reset();
        slow_.reset();
    }

private:
    // Expectancy-based size scalar (when trading is allowed)
    [[nodiscard]] double expectancy_scalar() const noexcept {
        double e = authority_expectancy();
        if (e <= 0.0)  return 0.0;
        if (e < 0.2)   return 0.5;
        if (e < 0.4)   return 1.0;
        if (e < 0.6)   return 1.3;
        return 1.5;  // Capped
    }
    
    Config cfg_;
    ExpectancyHorizon fast_;
    ExpectancyHorizon slow_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Decision string helper
// ─────────────────────────────────────────────────────────────────────────────
inline const char* decision_str(ExpectancyAuthority::Decision d) noexcept {
    switch (d) {
        case ExpectancyAuthority::Decision::FULL_SIZE:    return "FULL_SIZE";
        case ExpectancyAuthority::Decision::REDUCED_SIZE: return "REDUCED_SIZE";
        case ExpectancyAuthority::Decision::PAUSED:       return "PAUSED";
        case ExpectancyAuthority::Decision::DISABLED:     return "DISABLED";
        default: return "UNKNOWN";
    }
}

} // namespace Risk
} // namespace Chimera
