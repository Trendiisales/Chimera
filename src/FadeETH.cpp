#include "FadeETH.hpp"
#include <iostream>
#include <cmath>

FadeETH::FadeETH(const Config& cfg) : cfg_(cfg) {
    std::cout << "[FadeETH] Initialized with:" << std::endl;
    std::cout << "  Notional: $" << cfg_.notional_usd << std::endl;
    std::cout << "  Min Edge: " << cfg_.min_edge_score << std::endl;
    std::cout << "  OFI Z Min: " << cfg_.ofi_z_minimum << std::endl;
    std::cout << "  Impulse Min: " << cfg_.price_impulse_min_bps << " bps" << std::endl;
    std::cout << "  TP: " << cfg_.take_profit_bps << " bps, SL: " << cfg_.stop_loss_bps << " bps" << std::endl;
}

bool FadeETH::check_daily_limit(uint64_t now_ns) {
    uint64_t current_day = now_ns / (86400ULL * 1000000000ULL);
    if (current_day != last_reset_day_) {
        daily_pnl_ = 0.0;
        last_reset_day_ = current_day;
    }
    
    if (daily_pnl_ <= cfg_.daily_loss_limit_bps) {
        std::cout << "[FadeETH] Daily loss limit hit: " << daily_pnl_ << " bps" << std::endl;
        return false;
    }
    return true;
}

double FadeETH::compute_edge_score(double ofi_z, double ofi_accel, double impulse_bps,
                                     double btc_impulse_bps, double btc_ofi_z, double funding_rate_bps) {
    double score = 0.0;
    score += std::abs(ofi_z) * 0.4;
    score += std::abs(ofi_accel) * 0.2;
    score += (std::abs(impulse_bps) / cfg_.price_impulse_min_bps) * 0.3;
    double btc_factor = (std::abs(btc_impulse_bps) > 0.5) ? 
                        ((impulse_bps * btc_impulse_bps > 0) ? 0.1 : -0.1) : 0.0;
    score += btc_factor;
    return score;
}

std::optional<OrderIntent> FadeETH::generate_entry_signal(double bid, double ask,
                                                            double bid_depth, double ask_depth,
                                                            double edge_score, uint64_t now_ns) {
    double mid = (bid + ask) * 0.5;
    double spread_bps = ((ask - bid) / mid) * 10000.0;
    
    if (spread_bps > 4.0) return std::nullopt;
    
    double depth_ratio = bid_depth / (ask_depth + 1e-10);
    if (depth_ratio < 0.3 || depth_ratio > 3.0) return std::nullopt;
    
    if (edge_score < cfg_.min_edge_score) return std::nullopt;
    
    std::string side = (last_edge_score_ > 0) ? "SELL" : "BUY";
    double qty = cfg_.notional_usd / mid;
    qty = std::floor(qty * 1000.0) / 1000.0;
    
    if (qty < 0.001) return std::nullopt;
    
    std::cout << "[FadeETH] 🔥 SIGNAL GENERATED!" << std::endl;
    std::cout << "  Edge: " << edge_score << std::endl;
    std::cout << "  Side: " << side << std::endl;
    std::cout << "  Price: " << mid << std::endl;
    std::cout << "  Qty: " << qty << std::endl;
    std::cout << "  Spread: " << spread_bps << " bps" << std::endl;
    
    OrderIntent intent;
    intent.symbol = "ETHUSDT";
    intent.side = side;
    intent.quantity = qty;
    intent.price = (side == "BUY") ? bid : ask;
    intent.is_market = false;
    
    return intent;
}

std::optional<OrderIntent> FadeETH::check_exit_conditions(double mid, uint64_t now_ns) {
    if (position_qty_ == 0.0) return std::nullopt;
    
    double pnl_bps;
    if (position_side_ == "BUY") {
        pnl_bps = ((mid - position_entry_price_) / position_entry_price_) * 10000.0;
    } else {
        pnl_bps = ((position_entry_price_ - mid) / position_entry_price_) * 10000.0;
    }
    
    uint64_t hold_time_ms = (now_ns - position_entry_time_) / 1000000ULL;
    if (hold_time_ms > cfg_.micro_kill_ms) {
        std::cout << "[FadeETH] ⏰ MICRO KILL - Hold time: " << hold_time_ms << "ms" << std::endl;
        std::cout << "  PnL at exit: " << pnl_bps << " bps" << std::endl;
        
        OrderIntent exit;
        exit.symbol = "ETHUSDT";
        exit.side = (position_side_ == "BUY") ? "SELL" : "BUY";
        exit.quantity = std::abs(position_qty_);
        exit.price = 0;
        exit.is_market = true;
        return exit;
    }
    
    if (pnl_bps >= cfg_.take_profit_bps) {
        std::cout << "[FadeETH] ✅ TAKE PROFIT - PnL: " << pnl_bps << " bps" << std::endl;
        
        OrderIntent exit;
        exit.symbol = "ETHUSDT";
        exit.side = (position_side_ == "BUY") ? "SELL" : "BUY";
        exit.quantity = std::abs(position_qty_);
        exit.price = 0;
        exit.is_market = true;
        return exit;
    }
    
    if (pnl_bps <= cfg_.stop_loss_bps) {
        std::cout << "[FadeETH] ❌ STOP LOSS - PnL: " << pnl_bps << " bps" << std::endl;
        
        OrderIntent exit;
        exit.symbol = "ETHUSDT";
        exit.side = (position_side_ == "BUY") ? "SELL" : "BUY";
        exit.quantity = std::abs(position_qty_);
        exit.price = 0;
        exit.is_market = true;
        return exit;
    }
    
    return std::nullopt;
}

std::optional<OrderIntent> FadeETH::onDepth(
    double bid, double ask,
    double bid_depth_usd, double ask_depth_usd,
    double ofi_z, double ofi_accel,
    double impulse_bps,
    double btc_impulse_bps, double btc_ofi_z,
    double funding_rate_bps,
    uint64_t now_ns) {
    
    if (!check_daily_limit(now_ns)) return std::nullopt;
    
    double mid = (bid + ask) * 0.5;
    
    if (is_in_position()) {
        return check_exit_conditions(mid, now_ns);
    }
    
    if (std::abs(ofi_z) < cfg_.ofi_z_minimum) return std::nullopt;
    if (std::abs(impulse_bps) < cfg_.price_impulse_min_bps) return std::nullopt;
    
    double edge_score = compute_edge_score(ofi_z, ofi_accel, impulse_bps, 
                                            btc_impulse_bps, btc_ofi_z, funding_rate_bps);
    last_edge_score_ = edge_score;
    
    return generate_entry_signal(bid, ask, bid_depth_usd, ask_depth_usd, edge_score, now_ns);
}

void FadeETH::onFill(const std::string& side, double fill_price, double fill_qty, uint64_t fill_time) {
    if (!is_in_position()) {
        position_side_ = side;
        position_qty_ = (side == "BUY") ? fill_qty : -fill_qty;
        position_entry_price_ = fill_price;
        position_entry_time_ = fill_time;
        
        std::cout << "[FadeETH] Position opened: " << side << " " << fill_qty 
                  << " @ " << fill_price << std::endl;
    } else {
        double pnl_bps;
        if (position_side_ == "BUY") {
            pnl_bps = ((fill_price - position_entry_price_) / position_entry_price_) * 10000.0;
        } else {
            pnl_bps = ((position_entry_price_ - fill_price) / position_entry_price_) * 10000.0;
        }
        
        realized_pnl_ += pnl_bps;
        daily_pnl_ += pnl_bps;
        
        std::cout << "[FadeETH] Position closed: " << side << " " << fill_qty 
                  << " @ " << fill_price << std::endl;
        std::cout << "  Trade PnL: " << pnl_bps << " bps" << std::endl;
        std::cout << "  Realized PnL: " << realized_pnl_ << " bps" << std::endl;
        std::cout << "  Daily PnL: " << daily_pnl_ << " bps" << std::endl;
        
        position_qty_ = 0.0;
        position_entry_price_ = 0.0;
        position_entry_time_ = 0;
        position_side_ = "";
    }
}

void FadeETH::onTick(uint64_t now_ns) {
    // Periodic housekeeping if needed
}
