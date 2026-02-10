#pragma once
#include <deque>
#include <cstdint>

class VelocityCalculator {
public:
    VelocityCalculator();
    void record(double mid_price, uint64_t ts_ms);
    double velocity() const;
    double ema_velocity() const;

private:
    struct Sample { double price; uint64_t ts; };
    std::deque<Sample> samples_;
    static constexpr size_t WINDOW = 20;
    static constexpr uint64_t EMA_HORIZON_MS = 600;
    
    mutable double ema_vel_ = 0.0;
    mutable bool ema_initialized_ = false;
};
