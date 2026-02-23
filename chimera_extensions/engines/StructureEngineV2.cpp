#include "StructureEngineV2.hpp"

namespace chimera {
namespace engines {

StructureEngineV2::StructureEngineV2(core::ThreadSafeQueue<core::OrderIntent>& output)
    : m_output_queue(output) {}

void StructureEngineV2::on_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp_ns) {
    if (!m_running.load())
        return;
    
    double mid = (bid + ask) * 0.5;
    m_regime.update(mid);
    
    RegimeSignal rs = m_regime.classify();
    
    if (rs.confidence > 0.7) {
        core::OrderIntent intent;
        intent.symbol = symbol;
        intent.quantity = 0.3 * rs.confidence;
        intent.price = ask;
        intent.is_buy = (rs.regime == RegimeType::TREND_UP || rs.regime == RegimeType::BREAKOUT);
        intent.engine = core::EngineType::STRUCTURE;
        intent.confidence = rs.confidence;
        intent.timestamp_ns = timestamp_ns;
        
        m_output_queue.push(intent);
    }
}

void StructureEngineV2::stop() {
    m_running.store(false);
}

} // namespace engines
} // namespace chimera
