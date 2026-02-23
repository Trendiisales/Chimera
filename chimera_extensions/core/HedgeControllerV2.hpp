#pragma once

#include "../core/OrderIntentTypes.hpp"
#include "../core/ThreadSafeQueue.hpp"
#include "../risk/CapitalAllocatorV3.hpp"
#include "../core/PerformanceTracker.hpp"

namespace chimera {
namespace core {

class HedgeControllerV2 {
public:
    HedgeControllerV2(ThreadSafeQueue<OrderIntent>& queue,
                      risk::CapitalAllocatorV3& allocator,
                      PerformanceTracker& perf);
    
    void evaluate(const std::string& symbol, double current_price);

private:
    bool should_hedge_structure();
    double compute_hedge_size(double price);

    ThreadSafeQueue<OrderIntent>& m_intent_queue;
    risk::CapitalAllocatorV3& m_allocator;
    PerformanceTracker& m_perf;
};

} // namespace core
} // namespace chimera
