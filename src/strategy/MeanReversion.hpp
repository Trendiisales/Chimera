#pragma once
#include "core/contract.hpp"
#include <string>
#include <array>
#include <cstdint>

namespace chimera {

class MeanReversion : public IEngine {
public:
    explicit MeanReversion(const std::string& symbol);
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;
    void onRestore();

private:
    static constexpr size_t WINDOW_SIZE = 32;
    
    std::string engine_id_;
    std::string symbol_;
    
    std::array<double, WINDOW_SIZE> prices_;
    size_t write_idx_;
    size_t count_;
    double sum_;
    
    static constexpr double MAX_POS = 0.15;
};

}
