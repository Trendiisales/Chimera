#ifndef IMPULSE_ENGINE_HPP
#define IMPULSE_ENGINE_HPP

#include "engine_signal.hpp"
#include <atomic>
#include <cmath>

class ImpulseEngine {
public:
    void ingest(double price, uint64_t ts_ns) {
        double prev_price = last_price_.load(std::memory_order_relaxed);
        uint64_t prev_ts = last_ts_.load(std::memory_order_relaxed);
        
        if (prev_price == 0.0) {
            last_price_.store(price, std::memory_order_relaxed);
            last_ts_.store(ts_ns, std::memory_order_relaxed);
            window_start_price_.store(price, std::memory_order_relaxed);
            window_start_ts_.store(ts_ns, std::memory_order_relaxed);
            return;
        }
        
        uint64_t window_start = window_start_ts_.load(std::memory_order_relaxed);
        if (ts_ns - window_start > window_ns_) {
            window_start_price_.store(price, std::memory_order_relaxed);
            window_start_ts_.store(ts_ns, std::memory_order_relaxed);
        }
        
        double start_price = window_start_price_.load(std::memory_order_relaxed);
        double displacement = (price - start_price) / start_price * 10000.0;
        displacement_bps_.store(displacement, std::memory_order_relaxed);
        
        double dt_sec = (ts_ns - prev_ts) * 1e-9;
        if (dt_sec > 0.0) {
            double price_change_bps = (price - prev_price) / prev_price * 10000.0;
            double instant_velocity = price_change_bps / dt_sec;
            
            double prev_vel = velocity_.load(std::memory_order_relaxed);
            double new_vel = 0.7 * prev_vel + 0.3 * instant_velocity;
            velocity_.store(new_vel, std::memory_order_relaxed);
            
            double prev_accel = acceleration_.load(std::memory_order_relaxed);
            double new_accel = (new_vel - prev_vel) / dt_sec;
            acceleration_.store(0.8 * prev_accel + 0.2 * new_accel, std::memory_order_relaxed);
        }
        
        last_price_.store(price, std::memory_order_relaxed);
        last_ts_.store(ts_ns, std::memory_order_release);
    }

    ImpulseSignal evaluate(uint64_t now_ns) const {
        ImpulseSignal sig;
        sig.source = "IMPULSE";
        sig.ts_ns = now_ns;
        
        double disp = displacement_bps_.load(std::memory_order_acquire);
        double vel = velocity_.load(std::memory_order_acquire);
        
        sig.displacement_bps = disp;
        sig.velocity = vel;
        
        bool displacement_ok = std::abs(disp) >= min_displacement_bps_;
        bool velocity_ok = std::abs(vel) >= min_velocity_bps_per_sec_;
        bool same_direction = (disp > 0 && vel > 0) || (disp < 0 && vel < 0);
        
        if (displacement_ok && velocity_ok && same_direction) {
            sig.fired = true;
            sig.open = true;
            
            if (disp > 0) {
                sig.side = Side::BUY;
                sig.buy_impulse = true;
            } else {
                sig.side = Side::SELL;
                sig.sell_impulse = true;
            }
            
            sig.confidence = std::min(std::abs(disp) / (min_displacement_bps_ * 2.0), 1.0);
        }
        
        return sig;
    }

    double displacement() const { return displacement_bps_.load(std::memory_order_acquire); }
    double velocity() const { return velocity_.load(std::memory_order_acquire); }
    bool isOpen() const { return std::abs(displacement()) >= min_displacement_bps_; }

    void setMinDisplacement(double bps) { min_displacement_bps_ = bps; }
    void setMinVelocity(double bps_per_sec) { min_velocity_bps_per_sec_ = bps_per_sec; }
    void setWindow(uint64_t ns) { window_ns_ = ns; }

private:
    std::atomic<double> last_price_{0.0};
    std::atomic<uint64_t> last_ts_{0};
    
    std::atomic<double> window_start_price_{0.0};
    std::atomic<uint64_t> window_start_ts_{0};
    
    std::atomic<double> displacement_bps_{0.0};
    std::atomic<double> velocity_{0.0};
    std::atomic<double> acceleration_{0.0};
    
    uint64_t window_ns_ = 500'000'000ULL;
    double min_displacement_bps_ = 5.0;
    double min_velocity_bps_per_sec_ = 10.0;
};

#endif
