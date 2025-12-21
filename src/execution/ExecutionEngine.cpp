#include "execution/ExecutionEngine.hpp"

#include <iostream>
#include <chrono>

#include "risk/RiskManager.hpp"
#include "execution/PositionTracker.hpp"
#include "engine/IntentQueue.hpp"

ExecutionEngine::ExecutionEngine(RiskManager& risk,
                                 PositionTracker& positions)
: risk_(risk),
  positions_(positions),
  running_(false) {}

void ExecutionEngine::start(IntentQueue& queue) {
    running_.store(true, std::memory_order_release);

    worker_ = std::thread([this, &queue]() {
        std::cout << "[EXEC] execution thread started" << std::endl;

        unsigned long long count = 0;

        while (running_.load(std::memory_order_acquire)) {
            Intent intent(Intent::BUY, "X", 0.0);
            if (queue.try_pop(intent)) {
                ++count;
                if ((count % 5) == 0) {
                    std::cout << "[EXEC] consumed intents=" << count << std::endl;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
}

ExecutionEngine::~ExecutionEngine() {
    running_.store(false, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
}
