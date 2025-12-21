#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>

#include "execution/ExecutionEngine.hpp"
#include "risk/RiskManager.hpp"
#include "execution/PositionTracker.hpp"
#include "engine/IntentQueue.hpp"
#include "strategy/StrategyRegistry.hpp"
#include "strategy/DummyStrategy.hpp"

static std::atomic<bool> g_running{true};
static void on_signal(int){ g_running.store(false); }

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    RiskManager risk(100000.0);
    PositionTracker positions;
    IntentQueue queue;

    ExecutionEngine exec(risk, positions);
    exec.start(queue);

    StrategyRegistry registry;
    registry.add(std::make_unique<DummyStrategy>(queue));

    while (g_running.load()) {
        registry.tick_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}
