#pragma once
#include "core/contract.hpp"
#include <string>
#include <array>
#include <cstdint>

namespace chimera {

class ImpulseReversion : public IEngine {
public:
    explicit ImpulseReversion(const std::string& symbol);
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;
    void onRestore();

private:
    static constexpr size_t WINDOW_SIZE = 10;
    
    std::string engine_id_;
    std::string symbol_;
    uint64_t last_submit_ns_;
    
    std::array<double, WINDOW_SIZE> price_window_;
    size_t head_;
    size_t count_;
    double window_sum_;
    uint64_t last_impulse_ns_;
    
    static constexpr double MAX_POS = 0.15;
    static constexpr double BASE_QTY = 0.01;
    static constexpr double EDGE_BPS = 12.0;
    static constexpr double INV_K = 0.4;
    static constexpr uint64_t THROTTLE_NS = 25'000'000ULL;
    static constexpr double IMPULSE_THRESHOLD_BPS = 25.0;
    static constexpr uint64_t IMPULSE_COOLDOWN_NS = 200'000'000ULL;
};

}
