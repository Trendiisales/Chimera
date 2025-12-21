#include "strategy/StrategyRegistry.hpp"

void StrategyRegistry::add(std::unique_ptr<Strategy> s) {
    strategies_.push_back(std::move(s));
}

void StrategyRegistry::tick_all() {
    for (auto& s : strategies_) s->tick();
}
