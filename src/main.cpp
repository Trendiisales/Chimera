#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>

#include "execution/ExecutionEngine.hpp"
#include "risk/RiskManager.hpp"
#include "execution/PositionTracker.hpp"
#include "engine/IntentQueue.hpp"

static std::atomic<bool> g_running{true};

static void on_signal(int) {
    g_running.store(false, std::memory_order_relaxed);
}

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    RiskManager risk(100000.0);
    PositionTracker positions;
    IntentQueue queue;

    ExecutionEngine exec(risk, positions);
    exec.start(queue);

    int i = 0;
    while (g_running.load(std::memory_order_relaxed)) {
        queue.push(Intent(
            (i % 2 == 0) ? Intent::BUY : Intent::SELL,
            "BTCUSDT",
            1.0
        ));
        ++i;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}
