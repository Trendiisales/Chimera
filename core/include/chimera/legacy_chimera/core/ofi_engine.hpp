#ifndef OFI_ENGINE_HPP
#define OFI_ENGINE_HPP

#include "chimera/core/engine_signal.hpp"
#include <atomic>
#include <cmath>

class OFIEngine {
public:
    void ingest(double qty, bool is_buy, uint64_t ts_ns) {
        double delta = is_buy ? qty : -qty;
        
        double prev_ema = ofi_ema_.load(std::memory_order_relaxed);
        double new_ema = alpha_ * delta + (1.0 - alpha_) * prev_ema;
        ofi_ema_.store(new_ema, std::memory_order_relaxed);
        
        window_[window_idx_] = new_ema;
        window_idx_ = (window_idx_ + 1) % WINDOW_SIZE;
        if (window_count_ < WINDOW_SIZE) window_count_++;
        
        if (window_count_ < MIN_SAMPLES) {
            zscore_.store(0.0, std::memory_order_relaxed);
            accel_.store(0.0, std::memory_order_relaxed);
            return;
        }
        
        double sum = 0.0;
        for (size_t i = 0; i < window_count_; ++i)
            sum += window_[i];
        double mean = sum / window_count_;
        
        double var = 0.0;
        for (size_t i = 0; i < window_count_; ++i) {
            double d = window_[i] - mean;
            var += d * d;
        }
        double stddev = std::sqrt(var / window_count_);
        
        double prev_z = zscore_.load(std::memory_order_relaxed);
        double new_z = (stddev > 1e-10) ? (new_ema - mean) / stddev : 0.0;
        
        zscore_.store(new_z, std::memory_order_relaxed);
        accel_.store(new_z - prev_z, std::memory_order_relaxed);
        last_ts_.store(ts_ns, std::memory_order_relaxed);
    }

    OFISignal evaluate(uint64_t now_ns) const {
        OFISignal sig;
        sig.source = "OFI";
        sig.ts_ns = now_ns;
        
        double z = zscore_.load(std::memory_order_acquire);
        double a = accel_.load(std::memory_order_acquire);
        
        sig.zscore = z;
        sig.accel = a;
        
        bool forced = std::abs(a) > accel_threshold_ && std::abs(z) > zscore_threshold_;
        
        if (forced) {
            sig.fired = true;
            sig.side = (z > 0) ? Side::BUY : Side::SELL;
            sig.confidence = std::min(std::abs(z) / 3.0, 1.0);
        }
        
        return sig;
    }

    double zscore() const { return zscore_.load(std::memory_order_acquire); }
    double accel() const { return accel_.load(std::memory_order_acquire); }

    void setThresholds(double zscore_thresh, double accel_thresh) {
        zscore_threshold_ = zscore_thresh;
        accel_threshold_ = accel_thresh;
    }

private:
    static constexpr size_t WINDOW_SIZE = 200;
    static constexpr size_t MIN_SAMPLES = 20;
    
    std::atomic<double> ofi_ema_{0.0};
    std::atomic<double> zscore_{0.0};
    std::atomic<double> accel_{0.0};
    std::atomic<uint64_t> last_ts_{0};
    
    double window_[WINDOW_SIZE] = {0};
    size_t window_idx_ = 0;
    size_t window_count_ = 0;
    
    double alpha_ = 0.05;
    double zscore_threshold_ = 1.5;
    double accel_threshold_ = 0.2;
};

#endif
