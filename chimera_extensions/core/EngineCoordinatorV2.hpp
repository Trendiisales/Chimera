#pragma once

#include "../core/ThreadSafeQueue.hpp"
#include "../core/OrderIntentTypes.hpp"
#include "../core/PerformanceTracker.hpp"
#include "../core/HedgeController.hpp"
#include "../engines/HFTEngineV2.hpp"
#include "../engines/StructureEngineV2.hpp"
#include "../risk/CapitalAllocatorV2.hpp"
#include "../risk/RiskGovernorV2.hpp"
#include "../execution/LatencyEngine.hpp"
#include <thread>
#include <atomic>
#include <functional>

namespace chimera {
namespace core {

class EngineCoordinatorV2 {
public:
    EngineCoordinatorV2(risk::CapitalAllocatorV2& allocator,
                        risk::RiskGovernorV2& risk,
                        execution::LatencyEngine& latency);
    
    ~EngineCoordinatorV2();
    
    void start();
    void stop();
    
    void route_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp_ns);
    void on_execution_callback(const std::string& order_id, double fill_price, double pnl);
    
    void set_execution_handler(std::function<void(const OrderIntent&)> handler);

private:
    void hft_engine_loop();
    void structure_engine_loop();
    void coordinator_loop();
    void rebalance_loop();

    ThreadSafeQueue<OrderIntent> m_intent_queue;
    ThreadSafeQueue<execution::ExecutionStats> m_telemetry_queue;

    risk::CapitalAllocatorV2& m_allocator;
    risk::RiskGovernorV2& m_risk;
    execution::LatencyEngine& m_latency;
    
    PerformanceTracker m_perf;
    HedgeController m_hedge;

    engines::HFTEngineV2 m_hft_engine;
    engines::StructureEngineV2 m_structure_engine;

    std::thread m_hft_thread;
    std::thread m_structure_thread;
    std::thread m_coordinator_thread;
    std::thread m_rebalance_thread;

    std::atomic<bool> m_running{false};
    
    std::function<void(const OrderIntent&)> m_execution_handler;
};

} // namespace core
} // namespace chimera
