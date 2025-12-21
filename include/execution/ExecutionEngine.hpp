#pragma once
#include "execution/Fill.hpp"
#include <atomic>

class RiskManager;
class PositionTracker;

class ExecutionEngine {
public:
    ExecutionEngine(
        RiskManager& risk,
        PositionTracker& positions
    );

    void submit_intent(
        const std::string& symbol,
        const std::string& side,
        double price,
        double qty
    );

    uint64_t intents_seen() const;

private:
    RiskManager& risk_;
    PositionTracker& positions_;
    std::atomic<uint64_t> intents_{0};
};
