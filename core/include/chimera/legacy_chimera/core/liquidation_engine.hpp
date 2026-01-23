#ifndef LIQUIDATION_ENGINE_HPP
#define LIQUIDATION_ENGINE_HPP

#include "chimera/core/engine_signal.hpp"
#include <atomic>
#include <cstdint>

class LiquidationEngine {
public:
    void ingest(double notional, bool is_long_liq, uint64_t ts_ns) {
        uint64_t window_start = ts_ns > window_ns_ ? ts_ns - window_ns_ : 0;
        
        if (is_long_liq) {
            if (long_start_ts_.load(std::memory_order_relaxed) < window_start) {
                long_intensity_.store(notional, std::memory_order_relaxed);
                long_start_ts_.store(ts_ns, std::memory_order_relaxed);
            } else {
                double prev = long_intensity_.load(std::memory_order_relaxed);
                long_intensity_.store(prev + notional, std::memory_order_relaxed);
            }
        } else {
            if (short_start_ts_.load(std::memory_order_relaxed) < window_start) {
                short_intensity_.store(notional, std::memory_order_relaxed);
                short_start_ts_.store(ts_ns, std::memory_order_relaxed);
            } else {
                double prev = short_intensity_.load(std::memory_order_relaxed);
                short_intensity_.store(prev + notional, std::memory_order_relaxed);
            }
        }
        
        last_ts_.store(ts_ns, std::memory_order_release);
    }

    void decay(uint64_t now_ns) {
        uint64_t window_start = now_ns > window_ns_ ? now_ns - window_ns_ : 0;
        
        if (long_start_ts_.load(std::memory_order_relaxed) < window_start) {
            long_intensity_.store(0.0, std::memory_order_relaxed);
        }
        if (short_start_ts_.load(std::memory_order_relaxed) < window_start) {
            short_intensity_.store(0.0, std::memory_order_relaxed);
        }
    }

    LiqSignal evaluate(uint64_t now_ns) const {
        LiqSignal sig;
        sig.source = "LIQ";
        sig.ts_ns = now_ns;
        
        double long_int = long_intensity_.load(std::memory_order_acquire);
        double short_int = short_intensity_.load(std::memory_order_acquire);
        
        sig.intensity = long_int + short_int;
        
        bool long_spike = long_int > spike_threshold_;
        bool short_spike = short_int > spike_threshold_;
        
        if (long_spike && long_int > short_int * 1.5) {
            sig.fired = true;
            sig.side = Side::SELL;
            sig.is_long_cascade = true;
            sig.confidence = std::min(long_int / (spike_threshold_ * 2.0), 1.0);
        }
        else if (short_spike && short_int > long_int * 1.5) {
            sig.fired = true;
            sig.side = Side::BUY;
            sig.is_long_cascade = false;
            sig.confidence = std::min(short_int / (spike_threshold_ * 2.0), 1.0);
        }
        
        return sig;
    }

    double longIntensity() const { return long_intensity_.load(std::memory_order_acquire); }
    double shortIntensity() const { return short_intensity_.load(std::memory_order_acquire); }
    double totalIntensity() const { return longIntensity() + shortIntensity(); }

    void setThreshold(double thresh) { spike_threshold_ = thresh; }
    void setWindow(uint64_t ns) { window_ns_ = ns; }

private:
    std::atomic<double> long_intensity_{0.0};
    std::atomic<double> short_intensity_{0.0};
    std::atomic<uint64_t> long_start_ts_{0};
    std::atomic<uint64_t> short_start_ts_{0};
    std::atomic<uint64_t> last_ts_{0};
    
    uint64_t window_ns_ = 5'000'000'000ULL;
    double spike_threshold_ = 3'000'000.0;
};

#endif
