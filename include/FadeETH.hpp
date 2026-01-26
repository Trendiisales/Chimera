#pragma once
#include "OrderIntent.hpp"
#include <string>
#include <cstdint>
#include <optional>

class FadeETH {
public:
    struct Config {
        double notional_usd = 8000.0;
        double min_edge_score = 1.8;
        double ofi_z_minimum = 0.85;
        double price_impulse_min_bps = 1.2;
        uint64_t fade_stall_ms = 220;
        double take_profit_bps = 2.5;
        double stop_loss_bps = -2.8;
        uint64_t micro_kill_ms = 150;
        double daily_loss_limit_bps = -18.0;
    };

    FadeETH(const Config& cfg);
    
    std::optional<OrderIntent> onDepth(
        double bid, double ask,
        double bid_depth_usd, double ask_depth_usd,
        double ofi_z, double ofi_accel,
        double impulse_bps,
        double btc_impulse_bps, double btc_ofi_z,
        double funding_rate_bps,
        uint64_t now_ns
    );
    
    void onFill(const std::string& side, double fill_price, double fill_qty, uint64_t fill_time);
    void onTick(uint64_t now_ns);
    
    double get_pnl() const { return realized_pnl_; }
    bool is_in_position() const { return position_qty_ != 0.0; }
    
private:
    Config cfg_;
    
    // Position tracking
    double position_qty_ = 0.0;
    double position_entry_price_ = 0.0;
    uint64_t position_entry_time_ = 0;
    std::string position_side_;
    
    // PnL tracking
    double realized_pnl_ = 0.0;
    double daily_pnl_ = 0.0;
    uint64_t last_reset_day_ = 0;
    
    // Signal state
    double last_edge_score_ = 0.0;
    
    bool check_daily_limit(uint64_t now_ns);
    double compute_edge_score(double ofi_z, double ofi_accel, double impulse_bps, 
                               double btc_impulse_bps, double btc_ofi_z, double funding_rate_bps);
    std::optional<OrderIntent> generate_entry_signal(double bid, double ask, 
                                                       double bid_depth, double ask_depth,
                                                       double edge_score, uint64_t now_ns);
    std::optional<OrderIntent> check_exit_conditions(double mid, uint64_t now_ns);
};
