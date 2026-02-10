#include "execution/VelocityCalculator.hpp"
#include <cmath>

VelocityCalculator::VelocityCalculator() {}

void VelocityCalculator::record(double mid_price, uint64_t ts_ms)
{
    samples_.push_back({mid_price, ts_ms});
    if (samples_.size() > WINDOW)
        samples_.pop_front();
}

double VelocityCalculator::velocity() const
{
    if (samples_.size() < 5)
        return 0.0;
    
    const auto& first = samples_.front();
    const auto& last  = samples_.back();
    
    double dp = last.price - first.price;
    double dt = (last.ts - first.ts) / 1000.0;
    
    if (dt <= 0.0)
        return 0.0;
    
    return dp / dt;
}

double VelocityCalculator::ema_velocity() const
{
    if (samples_.size() < 2)
        return 0.0;
    
    // Calculate instantaneous velocity
    double inst_vel = velocity();
    
    if (!ema_initialized_) {
        ema_vel_ = inst_vel;
        ema_initialized_ = true;
        return ema_vel_;
    }
    
    // EMA with alpha tuned for ~600ms effective window
    constexpr double alpha = 0.12;
    ema_vel_ = alpha * inst_vel + (1.0 - alpha) * ema_vel_;
    
    return ema_vel_;
}
