#pragma once
#include "core/contract.hpp"
#include <deque>
#include <unordered_map>
#include <string>

namespace chimera {

class MeanReversion : public IEngine {
public:
    MeanReversion();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;

    // ---------------------------------------------------------------------------
    // Per-symbol state. A single shared window was mixing BTC@78000 and ETH@2400
    // into one 20-sample mean — the diff threshold fired on every symbol
    // transition. Each symbol needs its own independent window + sum.
    // ---------------------------------------------------------------------------
    struct SymbolState {
        std::deque<double> window;
        double sum{0.0};
    };
    std::unordered_map<std::string, SymbolState> state_;
};

}
