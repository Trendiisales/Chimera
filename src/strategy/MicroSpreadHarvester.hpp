#pragma once
#include "core/contract.hpp"
#include <string>

namespace chimera {

class MicroSpreadHarvester : public IEngine {
public:
    MicroSpreadHarvester();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;
    
    void onRestore();

private:
    std::string engine_id_;
    double last_spread_;
};

}
