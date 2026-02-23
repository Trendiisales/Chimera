#include "HedgeControllerV2.hpp"
#include <cmath>

namespace chimera {
namespace core {

HedgeControllerV2::HedgeControllerV2(ThreadSafeQueue<OrderIntent>& queue,
                                     risk::CapitalAllocatorV3& allocator,
                                     PerformanceTracker& perf)
    : m_intent_queue(queue), m_allocator(allocator), m_perf(perf) {}

bool HedgeControllerV2::should_hedge_structure() {
    double struct_score = m_perf.compute_score(EngineType::STRUCTURE);
    
    if (struct_score < 0.3)
        return true;
    
    return false;
}

double HedgeControllerV2::compute_hedge_size(double price) {
    double exposure = std::abs(m_allocator.get_net_exposure(EngineType::STRUCTURE));
    
    if (exposure <= 0.0)
        return 0.0;
    
    return (exposure * 0.25) / price;
}

void HedgeControllerV2::evaluate(const std::string& symbol, double current_price) {
    if (!should_hedge_structure())
        return;
    
    double qty = compute_hedge_size(current_price);
    if (qty <= 0.0)
        return;
    
    // FIX #5: CRITICAL - Correct hedge direction based on net exposure
    double net_exposure = m_allocator.get_net_exposure(EngineType::STRUCTURE);
    
    OrderIntent hedge;
    hedge.symbol = symbol;
    hedge.quantity = qty;
    hedge.price = current_price;
    
    // If structure is long (positive exposure), hedge with short
    // If structure is short (negative exposure), hedge with long
    hedge.is_buy = (net_exposure < 0);
    
    hedge.engine = EngineType::HFT;
    hedge.confidence = 0.8;
    
    m_intent_queue.push(hedge);
}

} // namespace core
} // namespace chimera
