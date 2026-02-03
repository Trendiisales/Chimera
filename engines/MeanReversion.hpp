#pragma once
#include "../core/contract.hpp"
#include <deque>

namespace chimera {

class MeanReversion : public IEngine {
public:
    MeanReversion();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;
    std::deque<double> window_;
    double sum_;
};

}
