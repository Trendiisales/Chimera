#pragma once
#include "engine/IntentQueue.hpp"

class DummyStrategy {
public:
    explicit DummyStrategy(IntentQueue& q) : q_(q), n_(0) {}
    void tick();
private:
    IntentQueue& q_;
    int n_;
};
