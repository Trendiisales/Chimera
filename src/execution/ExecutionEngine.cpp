#include "execution/ExecutionEngine.hpp"

#include <chrono>

#include "risk/RiskManager.hpp"
#include "execution/PositionTracker.hpp"
#include "engine/IntentQueue.hpp"
#include "metrics/HttpMetricsServer.hpp"

static HttpMetricsServer g_http_metrics(9102);

static inline std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
        while (running_.load(std::memory_order_acquire)) {
            Intent intent(Intent::BUY, "X", 0.0, now_ns());
            if (queue.try_pop(intent)) {
                g_http_metrics.inc_intents();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
}

ExecutionEngine::~ExecutionEngine() {
    running_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    g_http_metrics.stop();
}
