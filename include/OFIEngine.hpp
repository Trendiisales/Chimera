#pragma once
#include <deque>
#include <cmath>

struct OFIState {
    double z_score = 0.0;
    double accel = 0.0;
    double raw_ofi = 0.0;
};

class OFIEngine {
public:
    OFIEngine(uint64_t window_ms, uint64_t lookback_ms)
        : window_ms_(window_ms), lookback_ms_(lookback_ms) {}

    void onTrade(double qty, bool is_buyer_maker, uint64_t trade_time) {
        // Signed flow: aggressive buy = positive, aggressive sell = negative
        double signed_qty = is_buyer_maker ? -qty : qty;
        
        flow_window_.push_back({signed_qty, trade_time});
        
        // Remove trades outside window
        while (!flow_window_.empty() && 
               trade_time - flow_window_.front().time > window_ms_ * 1000000ULL) {
            flow_window_.pop_front();
        }
    }

    OFIState compute(uint64_t now_ns) {
        OFIState state;
        
        // Sum flow in current window
        double current_ofi = 0.0;
        for (const auto& f : flow_window_) {
            current_ofi += f.qty;
        }
        state.raw_ofi = current_ofi;
        
        // Add to history for z-score
        ofi_history_.push_back({current_ofi, now_ns});
        
        // Keep only lookback period
        while (!ofi_history_.empty() && 
               now_ns - ofi_history_.front().time > lookback_ms_ * 1000000ULL) {
            ofi_history_.pop_front();
        }
        
        // Compute z-score
        if (ofi_history_.size() >= 10) {
            double sum = 0.0, sum_sq = 0.0;
            for (const auto& h : ofi_history_) {
                sum += h.value;
                sum_sq += h.value * h.value;
            }
            double mean = sum / ofi_history_.size();
            double variance = (sum_sq / ofi_history_.size()) - (mean * mean);
            double std_dev = std::sqrt(std::max(variance, 1e-10));
            
            state.z_score = (current_ofi - mean) / std_dev;
            
            // Acceleration = rate of change of z-score
            if (ofi_history_.size() >= 3) {
                double prev_ofi = ofi_history_[ofi_history_.size() - 3].value;
                double prev_z = (prev_ofi - mean) / std_dev;
                state.accel = state.z_score - prev_z;
            }
        }
        
        return state;
    }

private:
    uint64_t window_ms_;
    uint64_t lookback_ms_;
    
    struct Flow {
        double qty;
        uint64_t time;
    };
    std::deque<Flow> flow_window_;
    
    struct OFISnapshot {
        double value;
        uint64_t time;
    };
    std::deque<OFISnapshot> ofi_history_;
};
