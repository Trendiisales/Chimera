// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/CapitalRampGovernor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Explicit capital scaling based on proven track record
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.15: NEW FILE - Prevents early overconfidence and AUM explosion
//
// PRINCIPLE: "Capital follows proof, not hope"
// - Scale up only after sustained profitability
// - Automatic reversion on drawdown
// - No manual override allowed
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <algorithm>
#include <iostream>
#include <ctime>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Capital Ramp Levels
// ─────────────────────────────────────────────────────────────────────────────
enum class RampLevel : uint8_t {
    MICRO   = 0,   // First 7 days: 0.25R max
    SMALL   = 1,   // 7-14 profitable days: 0.5R max
    NORMAL  = 2,   // 14-30 profitable days: 1.0R max
    SCALED  = 3,   // 30+ profitable days: 2.0R max
    
    COUNT = 4
};

inline const char* ramp_level_str(RampLevel level) noexcept {
    switch (level) {
        case RampLevel::MICRO:  return "MICRO(0.25R)";
        case RampLevel::SMALL:  return "SMALL(0.5R)";
        case RampLevel::NORMAL: return "NORMAL(1.0R)";
        case RampLevel::SCALED: return "SCALED(2.0R)";
        default: return "UNKNOWN";
    }
}

inline double ramp_level_max_risk(RampLevel level) noexcept {
    switch (level) {
        case RampLevel::MICRO:  return 0.25;
        case RampLevel::SMALL:  return 0.5;
        case RampLevel::NORMAL: return 1.0;
        case RampLevel::SCALED: return 2.0;
        default: return 0.25;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Capital Ramp Governor
// ─────────────────────────────────────────────────────────────────────────────
class CapitalRampGovernor {
public:
    struct Config {
        int days_for_small;      // Days to reach SMALL
        int days_for_normal;     // Days to reach NORMAL  
        int days_for_scaled;     // Days to reach SCALED
        double revert_dd_threshold;  // DD that triggers reversion
        
        Config()
            : days_for_small(7)
            , days_for_normal(14)
            , days_for_scaled(30)
            , revert_dd_threshold(1.5)
        {}
    };
    
    explicit CapitalRampGovernor(const Config& cfg = Config())
        : cfg_(cfg)
        , current_level_(RampLevel::MICRO)
        , profitable_days_(0)
        , total_days_(0)
        , peak_equity_(0.0)
        , current_equity_(0.0)
        , start_date_(0)
    {
        start_date_ = get_current_date();
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Daily update - call at end of each trading day
    // ─────────────────────────────────────────────────────────────────────────
    void end_of_day(double daily_pnl_R, double current_equity) noexcept {
        total_days_++;
        current_equity_ = current_equity;
        
        // Track peak for drawdown calculation
        if (current_equity > peak_equity_) {
            peak_equity_ = current_equity;
        }
        
        // Count profitable days
        if (daily_pnl_R > 0) {
            profitable_days_++;
        }
        
        // Calculate current drawdown
        double dd_R = 0.0;
        if (peak_equity_ > 0) {
            dd_R = (peak_equity_ - current_equity_) / (peak_equity_ * 0.01);  // Approx R units
        }
        
        // Check for reversion trigger
        if (dd_R >= cfg_.revert_dd_threshold && current_level_ > RampLevel::MICRO) {
            RampLevel old_level = current_level_;
            current_level_ = static_cast<RampLevel>(static_cast<int>(current_level_) - 1);
            
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  ⚠️  CAPITAL RAMP REVERSION                              ║\n";
            std::cout << "║  DD: " << dd_R << "R >= " << cfg_.revert_dd_threshold << "R threshold\n";
            std::cout << "║  " << ramp_level_str(old_level) << " → " << ramp_level_str(current_level_) << "\n";
            std::cout << "║  Max risk now: " << max_risk_R() << "R\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            
            // Reset profitable days counter after reversion
            profitable_days_ = 0;
            return;
        }
        
        // Check for promotion
        RampLevel new_level = calculate_level();
        if (new_level > current_level_) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  🟢 CAPITAL RAMP PROMOTION                               ║\n";
            std::cout << "║  Profitable days: " << profitable_days_ << "\n";
            std::cout << "║  " << ramp_level_str(current_level_) << " → " << ramp_level_str(new_level) << "\n";
            std::cout << "║  Max risk now: " << ramp_level_max_risk(new_level) << "R\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            
            current_level_ = new_level;
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Getters
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] RampLevel level() const noexcept { return current_level_; }
    [[nodiscard]] double max_risk_R() const noexcept { return ramp_level_max_risk(current_level_); }
    [[nodiscard]] int profitable_days() const noexcept { return profitable_days_; }
    [[nodiscard]] int total_days() const noexcept { return total_days_; }
    
    // Size multiplier based on ramp level (0.25 to 2.0)
    [[nodiscard]] double size_multiplier() const noexcept {
        return ramp_level_max_risk(current_level_);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Status
    // ─────────────────────────────────────────────────────────────────────────
    void print_status() const {
        std::cout << "[CAPITAL-RAMP] Level: " << ramp_level_str(current_level_)
                  << " | Profitable days: " << profitable_days_ << "/" << total_days_
                  << " | Max risk: " << max_risk_R() << "R\n";
    }

private:
    [[nodiscard]] RampLevel calculate_level() const noexcept {
        if (profitable_days_ >= cfg_.days_for_scaled) return RampLevel::SCALED;
        if (profitable_days_ >= cfg_.days_for_normal) return RampLevel::NORMAL;
        if (profitable_days_ >= cfg_.days_for_small)  return RampLevel::SMALL;
        return RampLevel::MICRO;
    }
    
    [[nodiscard]] static uint32_t get_current_date() noexcept {
        time_t now = time(nullptr);
        struct tm* t = gmtime(&now);
        return (t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday;
    }
    
    Config cfg_;
    RampLevel current_level_;
    int profitable_days_;
    int total_days_;
    double peak_equity_;
    double current_equity_;
    uint32_t start_date_;
};

} // namespace Risk
} // namespace Chimera
