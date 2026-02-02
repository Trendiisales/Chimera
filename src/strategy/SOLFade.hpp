#pragma once
#include <string>
#include <cstdint>
#include "core/contract.hpp"

namespace chimera {

class SOLFade : public IEngine {
public:
    SOLFade();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;
    uint64_t last_submit_ns_;
    
    static constexpr double MAX_POS = 0.05;
    static constexpr double BASE_QTY = 0.01;
    static constexpr double EDGE_BPS = 12.0;
    static constexpr double INV_K = 0.6;
    static constexpr uint64_t THROTTLE_NS = 15'000'000ULL;
};

}
