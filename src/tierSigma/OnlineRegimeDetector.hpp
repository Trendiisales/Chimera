#pragma once
#include <vector>
#include <cmath>

namespace chimera {

// Market regime types
enum class MarketRegime {
    TRENDING_UP,
    TRENDING_DOWN,
    RANGING,
    VOLATILE,
    CALM
};

// OnlineRegimeDetector: Detect market regime in real-time
class OnlineRegimeDetector {
public:
    OnlineRegimeDetector(size_t window = 100) 
        : window_size_(window) {}
    
    void on_price(double price) {
        prices_.push_back(price);
        if (prices_.size() > window_size_) {
            prices_.erase(prices_.begin());
        }
        
        if (prices_.size() >= 20) {
            update_regime();
        }
    }
    
    MarketRegime current_regime() const {
        return regime_;
    }
    
    double regime_confidence() const {
        return confidence_;
    }
    
    // Get recommended edge adjustment for current regime
    double edge_adjustment() const {
        switch (regime_) {
            case MarketRegime::TRENDING_UP:
            case MarketRegime::TRENDING_DOWN:
                return 1.5;  // Wider edge in trends
            case MarketRegime::VOLATILE:
                return 2.0;  // Much wider in volatility
            case MarketRegime::CALM:
                return 0.7;  // Tighter in calm markets
            case MarketRegime::RANGING:
            default:
                return 1.0;  // Normal
        }
    }

private:
    void update_regime() {
        double trend = calc_trend();
        double vol = calc_volatility();
        
        if (std::abs(trend) > 0.5 && vol < 1.0) {
            regime_ = (trend > 0) ? MarketRegime::TRENDING_UP : MarketRegime::TRENDING_DOWN;
            confidence_ = std::abs(trend);
        } else if (vol > 2.0) {
            regime_ = MarketRegime::VOLATILE;
            confidence_ = std::min(vol / 3.0, 1.0);
        } else if (vol < 0.3) {
            regime_ = MarketRegime::CALM;
            confidence_ = 1.0 - vol;
        } else {
            regime_ = MarketRegime::RANGING;
            confidence_ = 0.5;
        }
    }
    
    double calc_trend() const {
        if (prices_.size() < 2) return 0.0;
        
        // Simple linear regression
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
        size_t n = prices_.size();
        
        for (size_t i = 0; i < n; ++i) {
            sum_x += i;
            sum_y += prices_[i];
            sum_xy += i * prices_[i];
            sum_xx += i * i;
        }
        
        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
        double avg = sum_y / n;
        
        return (avg > 0) ? (slope / avg) : 0.0;  // Normalized slope
    }
    
    double calc_volatility() const {
        if (prices_.size() < 2) return 0.0;
        
        // Calculate standard deviation of returns
        std::vector<double> returns;
        for (size_t i = 1; i < prices_.size(); ++i) {
            double ret = (prices_[i] - prices_[i-1]) / prices_[i-1];
            returns.push_back(ret);
        }
        
        double mean = 0.0;
        for (double r : returns) mean += r;
        mean /= returns.size();
        
        double var = 0.0;
        for (double r : returns) {
            double diff = r - mean;
            var += diff * diff;
        }
        var /= returns.size();
        
        return std::sqrt(var) * 100.0;  // Convert to percentage
    }
    
    size_t window_size_;
    std::vector<double> prices_;
    MarketRegime regime_{MarketRegime::RANGING};
    double confidence_{0.5};
};

} // namespace chimera
