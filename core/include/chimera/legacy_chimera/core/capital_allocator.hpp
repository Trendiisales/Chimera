#ifndef CAPITAL_ALLOCATOR_HPP
#define CAPITAL_ALLOCATOR_HPP

#include <string>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cmath>

struct StreamCapital {
    double weight = 1.0;
    double equity = 0.0;
    double peak = 0.0;
    double drawdown = 0.0;
    int trade_count = 0;
    int win_count = 0;
};

class CapitalAllocator {
public:
    void registerStream(const std::string& name, double weight) {
        std::lock_guard<std::mutex> lock(mtx_);
        streams_[name] = {weight, 0.0, 0.0, 0.0, 0, 0};
    }

    double sizeFor(const std::string& name, double base_size) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = streams_.find(name);
        if (it == streams_.end())
            return base_size;
        
        auto& s = it->second;
        
        double dd_factor = 1.0 - s.drawdown;
        dd_factor = std::clamp(dd_factor, 0.1, 1.0);
        
        double win_rate = (s.trade_count > 10) 
            ? static_cast<double>(s.win_count) / s.trade_count 
            : 0.5;
        double wr_factor = std::clamp(win_rate / 0.5, 0.5, 1.5);
        
        return base_size * s.weight * dd_factor * wr_factor;
    }

    void onPnL(const std::string& name, double pnl) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = streams_.find(name);
        if (it == streams_.end())
            return;
        
        auto& s = it->second;
        s.equity += pnl;
        s.peak = std::max(s.peak, s.equity);
        
        if (s.peak > 0.0)
            s.drawdown = (s.peak - s.equity) / s.peak;
        else
            s.drawdown = 0.0;
        
        s.trade_count++;
        if (pnl > 0.0)
            s.win_count++;
    }

    bool allowed(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto it = streams_.find(name);
        if (it == streams_.end())
            return true;
        
        if (it->second.drawdown >= max_dd_)
            return false;
        
        if (total_drawdown_unlocked() >= max_total_dd_)
            return false;
        
        return true;
    }

    double totalEquity() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_equity_unlocked();
    }

    double totalDrawdown() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_drawdown_unlocked();
    }

    StreamCapital getStream(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = streams_.find(name);
        if (it != streams_.end())
            return it->second;
        return {};
    }

    void setMaxDrawdown(double dd) {
        std::lock_guard<std::mutex> lock(mtx_);
        max_dd_ = dd;
    }

    void setMaxTotalDrawdown(double dd) {
        std::lock_guard<std::mutex> lock(mtx_);
        max_total_dd_ = dd;
    }

    bool killSwitch() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return total_drawdown_unlocked() >= kill_threshold_;
    }

    void setKillThreshold(double thresh) {
        std::lock_guard<std::mutex> lock(mtx_);
        kill_threshold_ = thresh;
    }

private:
    double total_equity_unlocked() const {
        double sum = 0.0;
        for (auto& kv : streams_)
            sum += kv.second.equity;
        return sum;
    }

    double total_drawdown_unlocked() const {
        double total_eq = total_equity_unlocked();
        double peak = 0.0;
        for (auto& kv : streams_)
            peak += kv.second.peak;
        
        if (peak <= 0.0)
            return 0.0;
        
        return (peak - total_eq) / peak;
    }

    mutable std::mutex mtx_;
    std::unordered_map<std::string, StreamCapital> streams_;
    
    double max_dd_ = 0.20;
    double max_total_dd_ = 0.25;
    double kill_threshold_ = 0.30;
};

#endif
