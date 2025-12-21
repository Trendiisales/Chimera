#pragma once
#include "strategy/Strategy.hpp"

class DummyStrategy final : public Strategy {
public:
    explicit DummyStrategy(IntentQueue& q) : q_(q), n_(0) {}
    void tick() override;
private:
    IntentQueue& q_;
    int n_;
};
