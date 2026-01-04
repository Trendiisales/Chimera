// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/risk/PortfolioGovernor.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Cross-symbol correlation and portfolio-level risk control
// OWNER: Jo
// LAST VERIFIED: 2024-12-25
//
// v7.14: NEW FILE - Prevents multi-symbol blowups
//
// INVARIANT: "Portfolio survives, symbols are expendable"
// - Symbols are managed independently for signals
// - But portfolio risk is managed collectively
// - Correlated symbols share risk budget
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <array>
#include <vector>
#include <algorithm>
#include <iostream>

namespace Chimera {
namespace Risk {

// ─────────────────────────────────────────────────────────────────────────────
// Correlation Groups
// ─────────────────────────────────────────────────────────────────────────────
enum class CorrelationGroup : uint8_t {
    CRYPTO_MAJOR = 0,   // BTC, ETH
    CRYPTO_ALT   = 1,   // SOL, etc
    US_INDICES   = 2,   // NAS100, SPX500, US30
    METALS       = 3,   // XAUUSD, XAGUSD
    FOREX_USD    = 4,   // EURUSD, GBPUSD, USDJPY, etc
    FOREX_CROSS  = 5,   // Non-USD pairs
    UNCORRELATED = 6,   // Default
    
    COUNT = 7
};

inline const char* group_str(CorrelationGroup g) noexcept {
    switch (g) {
        case CorrelationGroup::CRYPTO_MAJOR: return "CRYPTO_MAJOR";
        case CorrelationGroup::CRYPTO_ALT:   return "CRYPTO_ALT";
        case CorrelationGroup::US_INDICES:   return "US_INDICES";
        case CorrelationGroup::METALS:       return "METALS";
        case CorrelationGroup::FOREX_USD:    return "FOREX_USD";
        case CorrelationGroup::FOREX_CROSS:  return "FOREX_CROSS";
        case CorrelationGroup::UNCORRELATED: return "UNCORRELATED";
        default: return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol to Group mapping
// ─────────────────────────────────────────────────────────────────────────────
inline CorrelationGroup get_correlation_group(const std::string& symbol) noexcept {
    // Crypto
    if (symbol == "BTCUSDT" || symbol == "ETHUSDT") return CorrelationGroup::CRYPTO_MAJOR;
    if (symbol == "SOLUSDT" || symbol == "BNBUSDT" || symbol == "XRPUSDT") 
        return CorrelationGroup::CRYPTO_ALT;
    
    // US Indices
    if (symbol == "NAS100" || symbol == "SPX500" || symbol == "US30")
        return CorrelationGroup::US_INDICES;
    
    // Metals
    if (symbol == "XAUUSD" || symbol == "XAGUSD")
        return CorrelationGroup::METALS;
    
    // Forex USD pairs
    if (symbol == "EURUSD" || symbol == "GBPUSD" || symbol == "USDJPY" ||
        symbol == "USDCAD" || symbol == "AUDUSD" || symbol == "USDCHF" ||
        symbol == "NZDUSD")
        return CorrelationGroup::FOREX_USD;
    
    // Forex crosses
    if (symbol == "EURGBP" || symbol == "EURJPY" || symbol == "GBPJPY" ||
        symbol == "AUDNZD")
        return CorrelationGroup::FOREX_CROSS;
    
    return CorrelationGroup::UNCORRELATED;
}

// ─────────────────────────────────────────────────────────────────────────────
// Portfolio Governor
// ─────────────────────────────────────────────────────────────────────────────
class PortfolioGovernor {
public:
    struct Config {
        double max_group_risk_R = 1.5;      // Max risk per correlation group (in R units)
        double max_portfolio_risk_R = 3.0;  // Max total portfolio risk
        double daily_loss_limit_R = 5.0;    // Daily loss limit
        int max_concurrent_positions = 6;   // Across all symbols
        int max_group_positions = 3;        // Per correlation group
    };
    
    explicit PortfolioGovernor(const Config& cfg = Config()) : cfg_(cfg) {}
    
    // ─────────────────────────────────────────────────────────────────────────
    // Risk tracking
    // ─────────────────────────────────────────────────────────────────────────
    
    // Add risk for a position
    void add_position(const std::string& symbol, double risk_R) noexcept {
        CorrelationGroup g = get_correlation_group(symbol);
        group_risk_[static_cast<size_t>(g)] += risk_R;
        total_risk_ += risk_R;
        group_positions_[static_cast<size_t>(g)]++;
        total_positions_++;
        symbol_risk_[symbol] = risk_R;
    }
    
    // Remove risk when position closes
    void remove_position(const std::string& symbol) noexcept {
        auto it = symbol_risk_.find(symbol);
        if (it == symbol_risk_.end()) return;
        
        double risk = it->second;
        CorrelationGroup g = get_correlation_group(symbol);
        
        group_risk_[static_cast<size_t>(g)] -= risk;
        total_risk_ -= risk;
        group_positions_[static_cast<size_t>(g)]--;
        total_positions_--;
        symbol_risk_.erase(it);
        
        // Prevent negative values from floating point errors
        if (group_risk_[static_cast<size_t>(g)] < 0) 
            group_risk_[static_cast<size_t>(g)] = 0;
        if (total_risk_ < 0) total_risk_ = 0;
    }
    
    // Record PnL (for daily tracking)
    void record_pnl(double pnl_R) noexcept {
        daily_pnl_ += pnl_R;
    }
    
    // Reset daily stats (call at session start)
    void reset_daily() noexcept {
        daily_pnl_ = 0.0;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // CAN WE ADD MORE RISK?
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] bool can_add_position(const std::string& symbol, double proposed_risk_R) const noexcept {
        // Check daily loss limit
        if (daily_pnl_ <= -cfg_.daily_loss_limit_R) {
            return false;
        }
        
        // Check total portfolio risk
        if (total_risk_ + proposed_risk_R > cfg_.max_portfolio_risk_R) {
            return false;
        }
        
        // Check total position count
        if (total_positions_ >= cfg_.max_concurrent_positions) {
            return false;
        }
        
        // Check correlation group risk
        CorrelationGroup g = get_correlation_group(symbol);
        size_t idx = static_cast<size_t>(g);
        
        if (group_risk_[idx] + proposed_risk_R > cfg_.max_group_risk_R) {
            return false;
        }
        
        // Check group position count
        if (group_positions_[idx] >= cfg_.max_group_positions) {
            return false;
        }
        
        return true;
    }
    
    // Get size scalar based on portfolio state (0.0 to 1.0)
    [[nodiscard]] double size_scalar(const std::string& symbol) const noexcept {
        // Daily loss scaling
        double daily_scalar = 1.0;
        if (daily_pnl_ < -cfg_.daily_loss_limit_R * 0.5) {
            daily_scalar = 0.5;  // Reduce to 50% after half daily limit hit
        }
        if (daily_pnl_ < -cfg_.daily_loss_limit_R * 0.75) {
            daily_scalar = 0.25; // Reduce to 25% at 75% of limit
        }
        
        // Portfolio utilization scaling
        double portfolio_util = total_risk_ / cfg_.max_portfolio_risk_R;
        double portfolio_scalar = 1.0 - (portfolio_util * 0.3);  // Max 30% reduction
        
        // Group utilization scaling
        CorrelationGroup g = get_correlation_group(symbol);
        double group_util = group_risk_[static_cast<size_t>(g)] / cfg_.max_group_risk_R;
        double group_scalar = 1.0 - (group_util * 0.3);
        
        return daily_scalar * portfolio_scalar * group_scalar;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // KILL SWITCHES
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] bool is_daily_limit_hit() const noexcept {
        return daily_pnl_ <= -cfg_.daily_loss_limit_R;
    }
    
    [[nodiscard]] bool is_portfolio_maxed() const noexcept {
        return total_risk_ >= cfg_.max_portfolio_risk_R;
    }
    
    [[nodiscard]] bool is_group_maxed(CorrelationGroup g) const noexcept {
        return group_risk_[static_cast<size_t>(g)] >= cfg_.max_group_risk_R;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // v7.15: CROSS-SYMBOL EXPECTANCY GOVERNOR
    // "Is the portfolio lying to me?"
    // ─────────────────────────────────────────────────────────────────────────
    
    void update_symbol_expectancy(const std::string& symbol, double expectancy_bps) noexcept {
        symbol_expectancy_[symbol] = expectancy_bps;
    }
    
    [[nodiscard]] double median_expectancy() const noexcept {
        if (symbol_expectancy_.empty()) return 0.0;
        
        std::vector<double> values;
        values.reserve(symbol_expectancy_.size());
        for (const auto& [sym, e] : symbol_expectancy_) {
            values.push_back(e);
        }
        
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        if (n % 2 == 0) {
            return (values[n/2 - 1] + values[n/2]) / 2.0;
        } else {
            return values[n/2];
        }
    }
    
    // Portfolio-wide expectancy check
    // Returns size multiplier based on portfolio health
    [[nodiscard]] double portfolio_expectancy_multiplier() const noexcept {
        double median = median_expectancy();
        
        if (median < -0.05) {
            // Median negative → pause new entries system-wide
            return 0.0;
        } else if (median < 0.0) {
            // Median slightly negative → reduce global size
            return 0.5;
        }
        return 1.0;
    }
    
    [[nodiscard]] bool portfolio_paused() const noexcept {
        return median_expectancy() < -0.05;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Getters
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] double total_risk() const noexcept { return total_risk_; }
    [[nodiscard]] double daily_pnl() const noexcept { return daily_pnl_; }
    [[nodiscard]] int total_positions() const noexcept { return total_positions_; }
    
    [[nodiscard]] double group_risk(CorrelationGroup g) const noexcept {
        return group_risk_[static_cast<size_t>(g)];
    }
    
    [[nodiscard]] int group_positions(CorrelationGroup g) const noexcept {
        return group_positions_[static_cast<size_t>(g)];
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Debug output
    // ─────────────────────────────────────────────────────────────────────────
    void print_status() const {
        std::cout << "\n=== PORTFOLIO GOVERNOR ===\n";
        std::cout << "Total Risk: " << total_risk_ << "R / " << cfg_.max_portfolio_risk_R << "R\n";
        std::cout << "Daily PnL:  " << daily_pnl_ << "R (limit: " << -cfg_.daily_loss_limit_R << "R)\n";
        std::cout << "Positions:  " << total_positions_ << " / " << cfg_.max_concurrent_positions << "\n";
        std::cout << "Median E:   " << median_expectancy() << " bps (mult: " << portfolio_expectancy_multiplier() << "x)\n";
        
        std::cout << "\nGroup Breakdown:\n";
        for (size_t i = 0; i < static_cast<size_t>(CorrelationGroup::COUNT); ++i) {
            if (group_positions_[i] > 0 || group_risk_[i] > 0.01) {
                std::cout << "  " << group_str(static_cast<CorrelationGroup>(i))
                          << ": " << group_risk_[i] << "R, " << group_positions_[i] << " pos\n";
            }
        }
        std::cout << "==========================\n\n";
    }

private:
    Config cfg_;
    
    std::array<double, static_cast<size_t>(CorrelationGroup::COUNT)> group_risk_{};
    std::array<int, static_cast<size_t>(CorrelationGroup::COUNT)> group_positions_{};
    
    double total_risk_ = 0.0;
    int total_positions_ = 0;
    double daily_pnl_ = 0.0;
    
    std::unordered_map<std::string, double> symbol_risk_;
    std::unordered_map<std::string, double> symbol_expectancy_;  // v7.15: Cross-symbol expectancy
};

} // namespace Risk
} // namespace Chimera
