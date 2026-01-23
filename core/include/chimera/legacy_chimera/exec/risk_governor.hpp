#ifndef RISK_GOVERNOR_HPP
#define RISK_GOVERNOR_HPP

#include "chimera/core/system_state.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <algorithm>

struct Position {
    double size = 0.0;
    double entry_price = 0.0;
    double pnl = 0.0;
    int trade_count = 0;
};

class RiskGovernor {
public:
    bool allowTrade(
        const std::string& symbol,
        Side side,
        double size,
        double price
    ) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (kill_switch_)
            return false;
        
        auto& pos = positions_[symbol];
        
        double new_size = pos.size;
        if (side == Side::BUY)
            new_size += size;
        else if (side == Side::SELL)
            new_size -= size;
        
        double ref_price = (price > 0.0) ? price : 
                          (pos.entry_price > 0.0 ? pos.entry_price : 1.0);
        double notional = std::abs(new_size) * ref_price;
        
        if (notional > max_notional_) {
            return false;
        }
        
        if (std::abs(new_size) > max_position_) {
            return false;
        }
        
        double total = totalNotional();
        if (total + notional > max_total_notional_) {
            return false;
        }
        
        if (daily_loss_ >= max_daily_loss_) {
            return false;
        }
        
        return true;
    }

    void onFill(
        const std::string& symbol,
        Side side,
        double size,
        double price
    ) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto& pos = positions_[symbol];
        
        if (side == Side::BUY) {
            if (pos.size >= 0) {
                double total_cost = pos.size * pos.entry_price + size * price;
                pos.size += size;
                if (pos.size > 0.0)
                    pos.entry_price = total_cost / pos.size;
            } else {
                double closed = std::min(size, std::abs(pos.size));
                double pnl = closed * (pos.entry_price - price);
                pos.pnl += pnl;
                daily_pnl_ += pnl;
                if (pnl < 0) daily_loss_ += std::abs(pnl);
                
                pos.size += size;
                if (pos.size > 0.0)
                    pos.entry_price = price;
            }
        } else {
            if (pos.size <= 0) {
                double total_cost = std::abs(pos.size) * pos.entry_price + size * price;
                pos.size -= size;
                if (pos.size < 0.0)
                    pos.entry_price = total_cost / std::abs(pos.size);
            } else {
                double closed = std::min(size, pos.size);
                double pnl = closed * (price - pos.entry_price);
                pos.pnl += pnl;
                daily_pnl_ += pnl;
                if (pnl < 0) daily_loss_ += std::abs(pnl);
                
                pos.size -= size;
                if (pos.size < 0.0)
                    pos.entry_price = price;
            }
        }
        
        pos.trade_count++;
    }

    Position getPosition(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mtx_);
        return positions_[symbol];
    }

    double totalNotional() {
        double sum = 0.0;
        for (auto& kv : positions_) {
            sum += std::abs(kv.second.size * kv.second.entry_price);
        }
        return sum;
    }

    void setKillSwitch(bool kill) {
        std::lock_guard<std::mutex> lock(mtx_);
        kill_switch_ = kill;
    }

    void resetDaily() {
        std::lock_guard<std::mutex> lock(mtx_);
        daily_pnl_ = 0.0;
        daily_loss_ = 0.0;
    }

    void setMaxNotional(double n) { max_notional_ = n; }
    void setMaxPosition(double p) { max_position_ = p; }
    void setMaxTotalNotional(double n) { max_total_notional_ = n; }
    void setMaxDailyLoss(double l) { max_daily_loss_ = l; }

    double dailyPnL() const { return daily_pnl_; }
    double dailyLoss() const { return daily_loss_; }
    bool isKilled() const { return kill_switch_; }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, Position> positions_;
    
    double max_notional_ = 10000.0;
    double max_position_ = 1.0;
    double max_total_notional_ = 50000.0;
    double max_daily_loss_ = 500.0;
    
    double daily_pnl_ = 0.0;
    double daily_loss_ = 0.0;
    bool kill_switch_ = false;
};

#endif
