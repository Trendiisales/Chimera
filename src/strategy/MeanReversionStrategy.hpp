#pragma once
#include "strategy/Strategy.hpp"
#include "strategy/StrategyContext.hpp"

class MeanReversionStrategy final : public Strategy {
public:
    explicit MeanReversionStrategy(StrategyContext& ctx) : ctx_(ctx), n_(0) {}
    void tick() override;
private:
    StrategyContext& ctx_;
    unsigned long long n_;
};
