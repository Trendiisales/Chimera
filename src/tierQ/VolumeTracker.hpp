#pragma once
#include <unordered_map>
#include <string>
#include <deque>

namespace chimera {

// VolumeTracker: Track trading volume and patterns
class VolumeTracker {
public:
    VolumeTracker(size_t window_size = 100) 
        : window_size_(window_size) {}
    
    void on_trade(const std::string& symbol, double qty, bool aggressor) {
        auto& hist = history_[symbol];
        hist.push_back({qty, aggressor});
        
        if (hist.size() > window_size_) {
            hist.pop_front();
        }
        
        update_metrics(symbol);
    }
    
    double avg_volume(const std::string& symbol) const {
        auto it = metrics_.find(symbol);
        return (it != metrics_.end()) ? it->second.avg_vol : 0.0;
    }
    
    double buy_pressure(const std::string& symbol) const {
        auto it = metrics_.find(symbol);
        return (it != metrics_.end()) ? it->second.buy_ratio : 0.5;
    }
    
    bool is_high_volume(const std::string& symbol, double threshold = 1.5) const {
        double recent = recent_volume(symbol, 10);
        double avg = avg_volume(symbol);
        return (avg > 0) && (recent / avg > threshold);
    }

private:
    struct Trade {
        double qty;
        bool aggressor;  // true = buy, false = sell
    };
    
    struct Metrics {
        double avg_vol{0.0};
        double buy_ratio{0.5};
    };
    
    double recent_volume(const std::string& symbol, size_t n) const {
        auto it = history_.find(symbol);
        if (it == history_.end()) return 0.0;
        
        double vol = 0.0;
        size_t count = std::min(n, it->second.size());
        for (size_t i = it->second.size() - count; i < it->second.size(); ++i) {
            vol += it->second[i].qty;
        }
        return vol;
    }
    
    void update_metrics(const std::string& symbol) {
        auto& hist = history_[symbol];
        if (hist.empty()) return;
        
        double total_vol = 0.0;
        double buy_vol = 0.0;
        
        for (const auto& trade : hist) {
            total_vol += trade.qty;
            if (trade.aggressor) buy_vol += trade.qty;
        }
        
        metrics_[symbol].avg_vol = total_vol / hist.size();
        metrics_[symbol].buy_ratio = (total_vol > 0) ? (buy_vol / total_vol) : 0.5;
    }
    
    size_t window_size_;
    std::unordered_map<std::string, std::deque<Trade>> history_;
    std::unordered_map<std::string, Metrics> metrics_;
};

} // namespace chimera
