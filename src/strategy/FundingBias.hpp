#pragma once
#include "core/contract.hpp"
#include <string>

namespace chimera {

class FundingBias : public IEngine {
public:
    FundingBias();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;
    
    void onFunding(double funding_rate);
    void onRestore();

private:
    std::string engine_id_;
    double funding_rate_;
};

}
