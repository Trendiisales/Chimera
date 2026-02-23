#pragma once

#include "../core/OrderIntentTypes.hpp"
#include "../core/ThreadSafeQueue.hpp"
#include "../risk/CapitalAllocatorV2.hpp"
#include "../core/PerformanceTracker.hpp"

namespace chimera {
namespace core {

class HedgeController {
public:
    HedgeController(ThreadSafeQueue<OrderIntent>& queue,
                    risk::CapitalAllocatorV2& allocator,
                    PerformanceTracker& perf);
    
    void evaluate(const std::string& symbol, double current_price);

private:
    bool should_hedge_structure();
    double compute_hedge_size(double price);

    ThreadSafeQueue<OrderIntent>& m_intent_queue;
    risk::CapitalAllocatorV2& m_allocator;
    PerformanceTracker& m_perf;
};

} // namespace core
} // namespace chimera
