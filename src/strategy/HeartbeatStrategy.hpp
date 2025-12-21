#pragma once
#include "strategy/Strategy.hpp"
#include "engine/IntentQueue.hpp"

class HeartbeatStrategy final : public Strategy {
public:
    explicit HeartbeatStrategy(IntentQueue& q) : q_(q), n_(0) {}
    void tick() override;
private:
    IntentQueue& q_;
    unsigned long long n_;
};
