#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>

// Include only tier1 headers (header-only, no linking needed)
#include "tier1/SignalRing.hpp"
#include "tier1/AtomicPositionGate.hpp"
#include "tier1/Tier1ExecutionRouter.hpp"
#include "tier1/Tier1BaseStrategy.hpp"

using namespace chimera;

// Minimal Context stub (no external dependencies)
struct MinimalContext {
    std::atomic<bool> running{true};
};

// Simple ExecutionRouter stub that doesn't depend on full Context
class SimpleRouter {
public:
    SimpleRouter(AtomicPositionGate& gate) : gate_(gate) {}

    void start() {
        running_.store(true);
        worker_ = std::thread([this]() { run(); });
    }

    void stop() {
        running_.store(false);
        if (worker_.joinable())
            worker_.join();
    }

    bool submit(const TradeSignal& sig) {
        return ring_.push(sig);
    }

    void set_cap(const std::string& sym, double cap) {
        gate_.set_cap(sym, cap);
    }

private:
    void run() {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
        std::cout << "[TIER1] Router pinned to core 0" << std::endl;
#endif
        std::cout << "[TIER1] Router thread started" << std::endl;

        TradeSignal sig;
        while (running_.load()) {
            while (ring_.pop(sig)) {
                process(sig);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }

        std::cout << "[TIER1] Router thread stopped" << std::endl;
    }

    void process(const TradeSignal& s) {
        std::string sym(s.symbol);
        std::string engine(s.engine_id);

        if (!s.reduce_only) {
            if (!gate_.allow(sym, s.qty)) {
                std::cout << "[TIER1] BLOCK " << engine << " " << sym
                          << " would exceed cap" << std::endl;
                return;
            }
        }

        gate_.apply_fill(sym, s.qty);

        std::cout << "[TIER1] FILL " << engine << " " << sym
                  << " qty=" << s.qty
                  << " @ " << s.price
                  << " edge=" << s.edge_bps << "bps"
                  << " pos=" << gate_.get_position(sym)
                  << std::endl;
    }

    SignalRing<4096> ring_;
    AtomicPositionGate& gate_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

// Simple strategy using lock-free submission
class SimpleStrategy {
public:
    SimpleStrategy(SimpleRouter& router,
                   AtomicPositionGate& gate,
                   const std::string& symbol,
                   const std::string& engine_id)
        : router_(router),
          cap_view_(gate, symbol),
          symbol_(symbol),
          engine_id_(engine_id) {}

    void run() {
        std::cout << "[" << engine_id_ << "] Strategy thread started" << std::endl;

        while (!g_shutdown.load()) {
            if (!cap_view_.can_trade(-0.01)) {
                // At cap, don't spam
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            TradeSignal sig{};
            std::strncpy(sig.symbol, symbol_.c_str(), 11);
            sig.symbol[11] = '\0';
            std::strncpy(sig.engine_id, engine_id_.c_str(), 11);
            sig.engine_id[11] = '\0';
            sig.qty = -0.01;
            sig.price = 2214.89;
            sig.edge_bps = 3.2;
            sig.ts_submit = now_ns();
            sig.reduce_only = false;

            router_.submit(sig);

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "[" << engine_id_ << "] Strategy thread stopped" << std::endl;
    }

private:
    uint64_t now_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    SimpleRouter& router_;
    CapView cap_view_;
    std::string symbol_;
    std::string engine_id_;
    static std::atomic<bool> g_shutdown;
};

std::atomic<bool> SimpleStrategy::g_shutdown{false};
static std::atomic<bool> g_main_shutdown{false};

void handle_sigint(int) {
    g_main_shutdown.store(true);
    SimpleStrategy::g_shutdown.store(true);
}

int main() {
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TIER 1 LOCK-FREE DEMO                ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    signal(SIGINT, handle_sigint);

    // Create position gate
    AtomicPositionGate gate;
    gate.set_cap("ETHUSDT", 0.05);
    gate.set_cap("BTCUSDT", 0.05);
    gate.set_cap("SOLUSDT", 0.05);

    // Create router
    SimpleRouter router(gate);
    router.start();

    // Create strategy
    SimpleStrategy eth_strategy(router, gate, "ETHUSDT", "ETH_FADE");

    // Run strategy in thread
    std::thread strategy_thread([&]() {
        eth_strategy.run();
    });

    std::cout << std::endl;
    std::cout << "System running. Press Ctrl+C to stop." << std::endl;
    std::cout << std::endl;

    // Monitor
    while (!g_main_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;

    router.stop();
    strategy_thread.join();

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
