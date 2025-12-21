#pragma once
#include "engine/IntentQueue.hpp"

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void tick() = 0;
};
