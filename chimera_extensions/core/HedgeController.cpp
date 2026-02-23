#include "HedgeController.hpp"

namespace chimera {
namespace core {

HedgeController::HedgeController(ThreadSafeQueue<OrderIntent>& queue,
                                 risk::CapitalAllocatorV2& allocator,
                                 PerformanceTracker& perf)
    : m_intent_queue(queue), m_allocator(allocator), m_perf(perf) {}

bool HedgeController::should_hedge_structure() {
    double struct_score = m_perf.compute_score(EngineType::STRUCTURE);
    
    if (struct_score < 0.3)
        return true;
    
    return false;
}

double HedgeController::compute_hedge_size(double price) {
    double exposure = m_allocator.get_engine_exposure(EngineType::STRUCTURE);
    
    if (exposure <= 0.0)
        return 0.0;
    
    return (exposure * 0.25) / price;
}

void HedgeController::evaluate(const std::string& symbol, double current_price) {
    if (!should_hedge_structure())
        return;
    
    double qty = compute_hedge_size(current_price);
    if (qty <= 0.0)
        return;
    
    OrderIntent hedge;
    hedge.symbol = symbol;
    hedge.quantity = qty;
    hedge.price = current_price;
    hedge.is_buy = false;
    hedge.engine = EngineType::HFT;
    hedge.confidence = 0.8;
    
    m_intent_queue.push(hedge);
}

} // namespace core
} // namespace chimera
