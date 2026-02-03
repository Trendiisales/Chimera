#pragma once
#include <string>

namespace chimera {

// ---------------------------------------------------------------------------
// Benchmark Scenarios: Standardized test workloads
// 
// Each scenario is designed to stress different aspects of the system:
//   1. Position Cap Stress - Tests gate blocking behavior
//   2. Multi-Strategy Contention - Tests mutex/lock-free performance
//   3. Burst Load - Tests backpressure handling
// ---------------------------------------------------------------------------

struct BenchmarkScenario {
    std::string name;
    int duration_seconds{30};
    int num_strategies{3};
    int signals_per_sec_per_strategy{10};
    double position_cap{0.05};  // Small cap to trigger blocks
    bool enable_position_violations{true};  // Generate signals that would violate cap
};

// Scenario 1: Position Cap Stress Test
// - Small position cap (0.05)
// - 3 strategies hammering same symbol
// - All trying to go in same direction (stress cap)
// - Expected: Lots of blocks in current system, clean in Tier1
inline BenchmarkScenario scenario_position_cap_stress() {
    BenchmarkScenario s;
    s.name = "PositionCapStress";
    s.duration_seconds = 30;
    s.num_strategies = 3;
    s.signals_per_sec_per_strategy = 10;  // 30 signals/sec total
    s.position_cap = 0.05;
    s.enable_position_violations = true;
    return s;
}

// Scenario 2: Multi-Strategy Contention
// - 5 strategies on different symbols
// - All generating simultaneously
// - Tests mutex contention vs lock-free
// - Expected: Lock contention in current, clean in Tier1
inline BenchmarkScenario scenario_multi_strategy_contention() {
    BenchmarkScenario s;
    s.name = "MultiStrategyContention";
    s.duration_seconds = 30;
    s.num_strategies = 5;
    s.signals_per_sec_per_strategy = 20;  // 100 signals/sec total
    s.position_cap = 0.10;
    s.enable_position_violations = false;  // Stay under cap
    return s;
}

// Scenario 3: Burst Load Test
// - Single strategy
// - Very high signal rate
// - Tests backpressure handling
// - Expected: Should handle cleanly in both systems
inline BenchmarkScenario scenario_burst_load() {
    BenchmarkScenario s;
    s.name = "BurstLoad";
    s.duration_seconds = 30;
    s.num_strategies = 1;
    s.signals_per_sec_per_strategy = 100;  // 100 signals/sec
    s.position_cap = 0.20;
    s.enable_position_violations = false;
    return s;
}

// Scenario 4: Realistic Trading
// - 3 strategies
// - Moderate signal rate
// - Mixed position sizes
// - Expected: This is "normal" operation
inline BenchmarkScenario scenario_realistic_trading() {
    BenchmarkScenario s;
    s.name = "RealisticTrading";
    s.duration_seconds = 60;
    s.num_strategies = 3;
    s.signals_per_sec_per_strategy = 5;  // 15 signals/sec total
    s.position_cap = 0.08;
    s.enable_position_violations = false;
    return s;
}

} // namespace chimera
