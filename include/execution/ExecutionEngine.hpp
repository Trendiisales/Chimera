#pragma once

#include <string>
#include <atomic>
#include <functional>
#include "execution/Fill.hpp"

class RiskManager;
class PositionTracker;
class CostModel;

class ExecutionEngine {
public:
    using FillCallback = std::function<void(const Fill&)>;

    ExecutionEngine(
        RiskManager& risk,
        PositionTracker& positions,
        CostModel& costs
    );

    void set_fill_callback(FillCallback cb);

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
    CostModel& costs_;

    FillCallback on_fill_;
    std::atomic<uint64_t> intents_{0};

    void emit_fill(const std::string& symbol, double qty, double price);
};
