#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>

class PercentileLogger {
public:
    PercentileLogger() : samples_(), log_interval_ms_(60000), last_log_ts_(0) {}
    
    void record(double value, uint64_t ts_ms) {
        samples_.push_back(value);
        
        if (ts_ms - last_log_ts_ >= log_interval_ms_) {
            flush();
            last_log_ts_ = ts_ms;
        }
    }
    
    void flush() {
        if (samples_.empty()) return;
        
        std::sort(samples_.begin(), samples_.end());
        
        size_t n = samples_.size();
        double p50 = samples_[n / 2];
        double p90 = samples_[(n * 9) / 10];
        double p99 = samples_[(n * 99) / 100];
        double max = samples_[n - 1];
        
        std::cout << "[RTT_PERCENTILES] n=" << n 
                  << " p50=" << p50 << "ms"
                  << " p90=" << p90 << "ms"
                  << " p99=" << p99 << "ms"
                  << " max=" << max << "ms\n";
        
        samples_.clear();
    }
    
private:
    std::vector<double> samples_;
    uint64_t log_interval_ms_;
    uint64_t last_log_ts_;
};
