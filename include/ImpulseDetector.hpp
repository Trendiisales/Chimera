#pragma once
#include <deque>
#include <cmath>

struct ImpulseState {
    double impulse_bps = 0.0;
    double velocity_bps_per_sec = 0.0;
    uint64_t stall_ms = 0;
};

class ImpulseDetector {
public:
    ImpulseDetector(uint64_t window_ms)
        : window_ms_(window_ms), last_price_(0), last_significant_move_time_(0) {}

    void onPrice(double price, uint64_t trade_time_ms) {
        // Initialize on first price
        if (last_price_ == 0.0) {
            last_price_ = price;
            last_significant_move_time_ = trade_time_ms;
            return;
        }
        
        price_history_.push_back({price, trade_time_ms});
        
        // Remove old prices
        while (!price_history_.empty() && 
               trade_time_ms - price_history_.front().time > window_ms_) {
            price_history_.pop_front();
        }
        
        // Check for significant move (>0.3 bps)
        double change_bps = std::abs((price - last_price_) / last_price_) * 10000.0;
        if (change_bps > 0.3) {
            last_significant_move_time_ = trade_time_ms;
        }
        
        last_price_ = price;
    }

    ImpulseState compute(uint64_t now_ns) {
        ImpulseState state;
        
        if (price_history_.size() < 2) return state;
        
        double start_price = price_history_.front().price;
        double end_price = price_history_.back().price;
        uint64_t time_span_ms = price_history_.back().time - price_history_.front().time;
        
        state.impulse_bps = ((end_price - start_price) / start_price) * 10000.0;
        
        if (time_span_ms > 0) {
            double time_span_sec = time_span_ms / 1000.0;
            state.velocity_bps_per_sec = state.impulse_bps / time_span_sec;
        }
        
        // FIX: Safe stall calculation with underflow protection
        uint64_t now_ms = now_ns / 1000000ULL;
        if (last_significant_move_time_ == 0) {
            last_significant_move_time_ = now_ms;
            state.stall_ms = 0;
        } else if (now_ms >= last_significant_move_time_) {
            state.stall_ms = now_ms - last_significant_move_time_;
        } else {
            // Clock went backwards - reset
            last_significant_move_time_ = now_ms;
            state.stall_ms = 0;
        }
        
        // Sanity: cap at 10 seconds
        if (state.stall_ms > 10000) {
            state.stall_ms = 10000;
        }
        
        return state;
    }

private:
    uint64_t window_ms_;
    double last_price_;
    uint64_t last_significant_move_time_;
    
    struct PricePoint {
        double price;
        uint64_t time;
    };
    std::deque<PricePoint> price_history_;
};
