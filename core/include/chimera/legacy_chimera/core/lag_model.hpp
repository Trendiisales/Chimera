#ifndef LAG_MODEL_HPP
#define LAG_MODEL_HPP

#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <cmath>
#include <cstdint>

struct PriceEvent {
    uint64_t ts_ns;
    double price;
    double delta_bps;
};

struct LagStats {
    double mean_lag_ms = 0.0;
    double correlation = 0.0;
    bool tradeable = false;
};

class LagModel {
public:
    void recordBTC(uint64_t ts_ns, double price) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        double delta = 0.0;
        if (btc_last_price_ > 0.0) {
            delta = (price - btc_last_price_) / btc_last_price_ * 10000.0;
        }
        
        btc_events_.push_back({ts_ns, price, delta});
        while (btc_events_.size() > window_size_)
            btc_events_.pop_front();
        
        btc_last_price_ = price;
        btc_last_ts_ = ts_ns;
    }

    void recordFollower(const std::string& sym, uint64_t ts_ns, double price) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto& data = followers_[sym];
        
        double delta = 0.0;
        if (data.last_price > 0.0) {
            delta = (price - data.last_price) / data.last_price * 10000.0;
        }
        
        data.events.push_back({ts_ns, price, delta});
        while (data.events.size() > window_size_)
            data.events.pop_front();
        
        data.last_price = price;
        data.last_ts = ts_ns;
        
        if (btc_last_ts_ > 0) {
            int64_t lag = static_cast<int64_t>(ts_ns) - static_cast<int64_t>(btc_last_ts_);
            data.lag_ema = 0.9 * data.lag_ema + 0.1 * (lag / 1e6);
        }
    }

    LagStats getStats(const std::string& sym) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        LagStats stats;
        auto it = followers_.find(sym);
        if (it == followers_.end())
            return stats;
        
        auto& data = it->second;
        stats.mean_lag_ms = data.lag_ema;
        
        if (btc_events_.size() < 20 || data.events.size() < 20)
            return stats;
        
        double corr = computeCorrelation(btc_events_, data.events);
        stats.correlation = corr;
        stats.tradeable = (corr > 0.6 && stats.mean_lag_ms > 5.0 && stats.mean_lag_ms < 500.0);
        
        return stats;
    }

    double getLagMs(const std::string& sym) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = followers_.find(sym);
        if (it == followers_.end())
            return 0.0;
        return it->second.lag_ema;
    }

private:
    struct FollowerData {
        std::deque<PriceEvent> events;
        double last_price = 0.0;
        uint64_t last_ts = 0;
        double lag_ema = 0.0;
    };

    double computeCorrelation(
        const std::deque<PriceEvent>& btc,
        const std::deque<PriceEvent>& follower
    ) {
        size_t n = std::min(btc.size(), follower.size());
        if (n < 10) return 0.0;
        
        double sum_x = 0, sum_y = 0, sum_xy = 0;
        double sum_x2 = 0, sum_y2 = 0;
        
        for (size_t i = 0; i < n; ++i) {
            double x = btc[btc.size() - n + i].delta_bps;
            double y = follower[follower.size() - n + i].delta_bps;
            
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
            sum_y2 += y * y;
        }
        
        double num = n * sum_xy - sum_x * sum_y;
        double den = std::sqrt((n * sum_x2 - sum_x * sum_x) * 
                               (n * sum_y2 - sum_y * sum_y));
        
        if (den < 1e-10) return 0.0;
        return num / den;
    }

    std::deque<PriceEvent> btc_events_;
    double btc_last_price_ = 0.0;
    uint64_t btc_last_ts_ = 0;
    
    std::unordered_map<std::string, FollowerData> followers_;
    
    size_t window_size_ = 200;
    std::mutex mtx_;
};

#endif
