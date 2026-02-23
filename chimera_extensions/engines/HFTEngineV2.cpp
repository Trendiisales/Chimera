#include "HFTEngineV2.hpp"
#include <chrono>

namespace chimera {
namespace engines {

HFTEngineV2::HFTEngineV2(core::ThreadSafeQueue<core::OrderIntent>& output)
    : m_output_queue(output) {}

void HFTEngineV2::on_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp_ns) {
    if (!m_running.load())
        return;
    
    double spread = ask - bid;
    m_micro.update_tick(bid, ask, 1.0, 1.0);
    
    MicroSignal sig = m_micro.compute_signal();
    
    if (sig.signal_strength > 0.6) {
        core::OrderIntent intent;
        intent.symbol = symbol;
        intent.quantity = 0.1 * sig.signal_strength;
        intent.price = sig.microprice;
        intent.is_buy = (sig.imbalance > 0);
        intent.engine = core::EngineType::HFT;
        intent.confidence = sig.signal_strength;
        intent.timestamp_ns = timestamp_ns;
        
        m_output_queue.push(intent);
    }
}

void HFTEngineV2::stop() {
    m_running.store(false);
}

} // namespace engines
} // namespace chimera
