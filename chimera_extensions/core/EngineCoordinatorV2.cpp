#include "EngineCoordinatorV2.hpp"
#include <chrono>

namespace chimera {
namespace core {

EngineCoordinatorV2::EngineCoordinatorV2(risk::CapitalAllocatorV2& allocator,
                                         risk::RiskGovernorV2& risk,
                                         execution::LatencyEngine& latency)
    : m_allocator(allocator)
    , m_risk(risk)
    , m_latency(latency)
    , m_hedge(m_intent_queue, m_allocator, m_perf)
    , m_hft_engine(m_intent_queue)
    , m_structure_engine(m_intent_queue) {}

EngineCoordinatorV2::~EngineCoordinatorV2() {
    if (m_running.load())
        stop();
}

void EngineCoordinatorV2::start() {
    m_running.store(true);
    
    m_hft_thread = std::thread(&EngineCoordinatorV2::hft_engine_loop, this);
    m_structure_thread = std::thread(&EngineCoordinatorV2::structure_engine_loop, this);
    m_coordinator_thread = std::thread(&EngineCoordinatorV2::coordinator_loop, this);
    m_rebalance_thread = std::thread(&EngineCoordinatorV2::rebalance_loop, this);
}

void EngineCoordinatorV2::stop() {
    m_running.store(false);
    
    m_hft_engine.stop();
    m_structure_engine.stop();
    
    if (m_hft_thread.joinable()) m_hft_thread.join();
    if (m_structure_thread.joinable()) m_structure_thread.join();
    if (m_coordinator_thread.joinable()) m_coordinator_thread.join();
    if (m_rebalance_thread.joinable()) m_rebalance_thread.join();
}

void EngineCoordinatorV2::route_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp_ns) {
    m_hft_engine.on_market_data(symbol, bid, ask, timestamp_ns);
    m_structure_engine.on_market_data(symbol, bid, ask, timestamp_ns);
    
    double mid = (bid + ask) * 0.5;
    m_hedge.evaluate(symbol, mid);
}

void EngineCoordinatorV2::on_execution_callback(const std::string& order_id, double fill_price, double pnl) {
    m_latency.on_fill(order_id, fill_price);
    m_risk.record_fill(pnl);
}

void EngineCoordinatorV2::set_execution_handler(std::function<void(const OrderIntent&)> handler) {
    m_execution_handler = handler;
}

void EngineCoordinatorV2::hft_engine_loop() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void EngineCoordinatorV2::structure_engine_loop() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void EngineCoordinatorV2::coordinator_loop() {
    while (m_running.load()) {
        OrderIntent intent;
        if (!m_intent_queue.try_pop(intent)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        if (!m_allocator.reserve(intent))
            continue;
        
        auto decision = m_risk.evaluate(intent);
        if (!decision.approved) {
            m_allocator.release(intent);
            m_risk.record_reject();
            continue;
        }
        
        intent.quantity *= decision.size_multiplier;
        
        m_latency.on_order_sent(intent.intent_id, intent.price, 0.2);
        
        if (m_execution_handler) {
            m_execution_handler(intent);
        }
        
        m_allocator.commit(intent);
    }
}

void EngineCoordinatorV2::rebalance_loop() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        double hft_weight = m_perf.get_allocation_weight(EngineType::HFT);
        double struct_weight = m_perf.get_allocation_weight(EngineType::STRUCTURE);
        
        m_allocator.update_engine_weights(hft_weight, struct_weight);
    }
}

} // namespace core
} // namespace chimera
