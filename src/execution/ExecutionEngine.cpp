#include "execution/ExecutionEngine.hpp"

#include <chrono>

#include "risk/RiskManager.hpp"
#include "execution/PositionTracker.hpp"
#include "engine/IntentQueue.hpp"
#include "metrics/HttpMetricsServer.hpp"

static HttpMetricsServer g_http_metrics(9102);

ExecutionEngine::ExecutionEngine(RiskManager& risk,
                                 PositionTracker& positions)
: risk_(risk),
  positions_(positions),
  running_(false) {
    g_http_metrics.start();
}

void ExecutionEngine::start(IntentQueue& queue) {
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this, &queue]() {
        Intent intent(Intent::BUY, "X", 0.0);
        while (running_.load(std::memory_order_acquire)) {
            if (queue.try_pop(intent)) {
                g_http_metrics.inc_intents();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
}

ExecutionEngine::~ExecutionEngine() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    g_http_metrics.stop();
}
