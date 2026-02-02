#pragma once
#include "core/contract.hpp"

namespace chimera {

class ETHSniper : public IEngine {
public:
    ETHSniper();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;
    double last_mid_;
};

}
