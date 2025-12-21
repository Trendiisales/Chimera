#pragma once
#include <vector>
#include <memory>
#include "strategy/Strategy.hpp"

class StrategyRegistry {
public:
    void add(std::unique_ptr<Strategy> s);
    void tick_all();
private:
    std::vector<std::unique_ptr<Strategy>> strategies_;
};
