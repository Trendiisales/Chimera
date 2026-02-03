// Benchmark harness for TIER1 system (lock-free)
// Uses same scenarios as current system for comparison

#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <iostream>
#include <signal.h>

#include "runtime/Context.hpp"
#include "tier1/Tier1ExecutionRouter.hpp"
#include "tier1/AtomicPositionGate.hpp"
#include "tier1/CapView.hpp"
#include "tier1/SignalRing.hpp"
#include "benchmark/BenchmarkMetrics.hpp"
#include "benchmark/BenchmarkScenarios.hpp"

using namespace chimera;

// Global benchmark metrics
BenchmarkMetrics g_benchmark;

// Shutdown flag
static std::atomic<bool> g_shutdown{false};

void handle_sigint(int) {
    g_shutdown.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Mock Strategy for Benchmarking (Tier1)
// Uses lock-free submission
// ---------------------------------------------------------------------------
class Tier1BenchmarkStrategy {
public:
    Tier1BenchmarkStrategy(Tier1ExecutionRouter& router,
                           AtomicPositionGate& gate,
                           const std::string& symbol,
                           const std::string& engine_id,
                           int signals_per_sec,
                           bool violate_caps)
        : router_(router),
          cap_view_(gate, symbol),
          symbol_(symbol),
          engine_id_(engine_id),
          signals_per_sec_(signals_per_sec),
          violate_caps_(violate_caps) {}

    void run() {
        int interval_ms = 1000 / signals_per_sec_;
        int signal_count = 0;
        
        while (!g_shutdown.load(std::memory_order_relaxed)) {
            // Generate signal
            g_benchmark.record_signal_generated();
            
            double qty = 0.01;  // Small size
            double price = 2200.0 + (signal_count % 100);  // Varying price
            
            // Tier1 advantage: Check cap BEFORE generating signal
            bool should_send = true;
            if (!violate_caps_) {
                if (!cap_view_.can_trade(qty)) {
                    should_send = false;
                    g_benchmark.record_block_position_cap();
                }
            }
            
            if (should_send) {
                g_benchmark.record_signal_submitted();
                
                uint64_t ts_submit = now_ns();
                
                // Build signal
                TradeSignal sig{};
                std::strncpy(sig.symbol, symbol_.c_str(), 11);
                sig.symbol[11] = '\0';
                std::strncpy(sig.engine_id, engine_id_.c_str(), 11);
                sig.engine_id[11] = '\0';
                sig.qty = qty;
                sig.price = price;
                sig.edge_bps = 2.5;
                sig.reduce_only = false;
                sig.ts_submit = ts_submit;
                
                // Submit (lock-free)
                bool submitted = router_.submit(sig);
                
                if (submitted) {
                    // In shadow mode, fill is instant
                    g_benchmark.record_fill(ts_submit);
                } else {
                    // Ring full (backpressure)
                    g_benchmark.record_block_backpressure();
                }
            }
            
            signal_count++;
            
            // Rate limiting
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }

private:
    uint64_t now_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    Tier1ExecutionRouter& router_;
    CapView cap_view_;
    std::string symbol_;
    std::string engine_id_;
    int signals_per_sec_;
    bool violate_caps_;
};

// ---------------------------------------------------------------------------
// Run Benchmark
// ---------------------------------------------------------------------------
void run_benchmark(const BenchmarkScenario& scenario) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "RUNNING BENCHMARK: " << scenario.name << "\n";
    std::cout << "System: TIER1 (Lock-Free)\n";
    std::cout << "========================================\n";
    std::cout << "Duration: " << scenario.duration_seconds << "s\n";
    std::cout << "Strategies: " << scenario.num_strategies << "\n";
    std::cout << "Signals/sec/strategy: " << scenario.signals_per_sec_per_strategy << "\n";
    std::cout << "Position cap: " << scenario.position_cap << "\n";
    std::cout << "========================================\n";
    std::cout << "\n";

    // Create context
    Context ctx;
    
    // Create atomic position gate
    AtomicPositionGate gate;
    
    // Set caps for all symbols
    for (int i = 0; i < scenario.num_strategies; ++i) {
        std::string symbol = "SYM" + std::to_string(i) + "USDT";
        gate.set_cap(symbol, scenario.position_cap);
    }
    
    // Create Tier1 router
    Tier1ExecutionRouter router(ctx, gate);
    router.start();  // Start router thread on core 0
    
    // Create strategies
    std::vector<std::unique_ptr<Tier1BenchmarkStrategy>> strategies;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < scenario.num_strategies; ++i) {
        std::string symbol = "SYM" + std::to_string(i) + "USDT";
        std::string engine_id = "BENCH_" + std::to_string(i);
        
        strategies.push_back(std::make_unique<Tier1BenchmarkStrategy>(
            router,
            gate,
            symbol,
            engine_id,
            scenario.signals_per_sec_per_strategy,
            scenario.enable_position_violations
        ));
    }
    
    // Start benchmark timer
    g_benchmark.start();
    
    // Start strategy threads
    for (auto& strat : strategies) {
        threads.emplace_back([&strat]() {
            strat->run();
        });
    }
    
    // Run for specified duration
    std::cout << "Benchmark running...\n";
    std::this_thread::sleep_for(std::chrono::seconds(scenario.duration_seconds));
    
    // Signal shutdown
    g_shutdown.store(true, std::memory_order_release);
    
    // Join threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Stop router
    router.stop();
    
    // Stop benchmark timer
    g_benchmark.stop();
    
    std::cout << "Benchmark complete.\n";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    signal(SIGINT, handle_sigint);
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║  TIER BENCHMARK - TIER1 SYSTEM        ║\n";
    std::cout << "║  (Lock-Free Architecture)             ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    // Select scenario
    BenchmarkScenario scenario;
    
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "cap") {
            scenario = scenario_position_cap_stress();
        } else if (arg == "contention") {
            scenario = scenario_multi_strategy_contention();
        } else if (arg == "burst") {
            scenario = scenario_burst_load();
        } else if (arg == "realistic") {
            scenario = scenario_realistic_trading();
        } else {
            std::cerr << "Unknown scenario: " << arg << "\n";
            std::cerr << "Options: cap, contention, burst, realistic\n";
            return 1;
        }
    } else {
        // Default: position cap stress
        scenario = scenario_position_cap_stress();
    }
    
    // Run benchmark
    run_benchmark(scenario);
    
    // Print results
    g_benchmark.print_summary("TIER1");
    
    // Save to CSV
    g_benchmark.save_csv("benchmark_results.csv", "TIER1");
    
    return 0;
}
