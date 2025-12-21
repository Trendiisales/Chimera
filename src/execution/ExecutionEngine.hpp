#pragma once

#include <atomic>
#include <thread>

class RiskManager;
class PositionTracker;
class IntentQueue;

class ExecutionEngine {
public:
    ExecutionEngine(RiskManager& risk,
                    PositionTracker& positions);

    ~ExecutionEngine();

    void start(IntentQueue& queue);

private:
    RiskManager& risk_;
    PositionTracker& positions_;
    std::atomic<bool> running_;
    std::thread worker_;
};
