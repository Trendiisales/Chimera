// =============================================================================
// ExpectancyAuthority.hpp - Shadow Trade Expectancy Tracking
// =============================================================================
// v4.12.0: Standalone implementation (previously in crypto_engine)
//
// PURPOSE: Track rolling expectancy of shadow trades for promotion decisions
// =============================================================================
#pragma once

#include <cstdint>
#include <atomic>
#include <algorithm>

namespace Chimera {
namespace Risk {

// =============================================================================
// ExpectancyAuthority - Rolling expectancy tracker for shadow trades
// =============================================================================
class ExpectancyAuthority {
public:
    struct Config {
        double min_expectancy_bps;
        int min_trades;
        double ewma_alpha;
        
        Config() 
            : min_expectancy_bps(0.5)
            , min_trades(10)
            , ewma_alpha(0.1)
        {}
    };
    
    explicit ExpectancyAuthority(const Config& cfg = Config()) 
        : config_(cfg)
        , trades_(0)
        , expectancy_(0.0)
        , win_count_(0)
        , total_pnl_(0.0)
    {}
    
    // Record a shadow trade result (pnl in basis points)
    void record(double pnl_bps) noexcept {
        trades_++;
        total_pnl_ += pnl_bps;
        
        if (pnl_bps > 0) win_count_++;
        
        // EWMA update of expectancy
        if (trades_ == 1) {
            expectancy_ = pnl_bps;
        } else {
            expectancy_ = config_.ewma_alpha * pnl_bps + (1.0 - config_.ewma_alpha) * expectancy_;
        }
    }
    
    // Fast hot-path queries
    double fast_expectancy() const noexcept { return expectancy_; }
    int fast_trades() const noexcept { return trades_; }
    
    // Is expectancy valid? (enough trades)
    bool is_valid() const noexcept {
        return trades_ >= config_.min_trades;
    }
    
    // Is expectancy positive enough for promotion?
    bool is_promotable() const noexcept {
        return is_valid() && expectancy_ >= config_.min_expectancy_bps;
    }
    
    // Statistics
    double win_rate() const noexcept {
        return trades_ > 0 ? static_cast<double>(win_count_) / trades_ : 0.0;
    }
    
    double average_pnl() const noexcept {
        return trades_ > 0 ? total_pnl_ / trades_ : 0.0;
    }
    
    // Reset
    void reset() noexcept {
        trades_ = 0;
        expectancy_ = 0.0;
        win_count_ = 0;
        total_pnl_ = 0.0;
    }
    
    Config& config() noexcept { return config_; }
    const Config& config() const noexcept { return config_; }

private:
    Config config_;
    int trades_;
    double expectancy_;
    int win_count_;
    double total_pnl_;
};

} // namespace Risk
} // namespace Chimera
