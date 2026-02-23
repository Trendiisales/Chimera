#include "CapitalAllocatorV2.hpp"

namespace chimera {
namespace risk {

CapitalAllocatorV2::CapitalAllocatorV2(double global_cap) : m_global_cap(global_cap) {}

double CapitalAllocatorV2::calculate_notional(const core::OrderIntent& intent) {
    return intent.quantity * intent.price;
}

bool CapitalAllocatorV2::reserve(const core::OrderIntent& intent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    double notional = calculate_notional(intent);
    
    double projected_global = m_global_exposure.reserved + m_global_exposure.committed + notional;
    if (projected_global > m_global_cap)
        return false;
    
    double engine_limit = (intent.engine == core::EngineType::HFT) 
        ? m_global_cap * m_dynamic_hft_weight
        : m_global_cap * m_dynamic_structure_weight;
    
    auto& engine_state = m_engine_exposure[intent.engine];
    double projected_engine = engine_state.reserved + engine_state.committed + notional;
    if (projected_engine > engine_limit)
        return false;
    
    auto& symbol_state = m_symbol_exposure[intent.symbol];
    double symbol_limit = m_global_cap * 0.5;
    double projected_symbol = symbol_state.reserved + symbol_state.committed + notional;
    if (projected_symbol > symbol_limit)
        return false;
    
    m_global_exposure.reserved += notional;
    engine_state.reserved += notional;
    symbol_state.reserved += notional;
    
    return true;
}

void CapitalAllocatorV2::commit(const core::OrderIntent& intent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    double notional = calculate_notional(intent);
    
    m_global_exposure.reserved -= notional;
    m_global_exposure.committed += notional;
    
    auto& engine_state = m_engine_exposure[intent.engine];
    engine_state.reserved -= notional;
    engine_state.committed += notional;
    
    auto& symbol_state = m_symbol_exposure[intent.symbol];
    symbol_state.reserved -= notional;
    symbol_state.committed += notional;
}

void CapitalAllocatorV2::release(const core::OrderIntent& intent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    double notional = calculate_notional(intent);
    
    m_global_exposure.reserved -= notional;
    
    auto& engine_state = m_engine_exposure[intent.engine];
    engine_state.reserved -= notional;
    
    auto& symbol_state = m_symbol_exposure[intent.symbol];
    symbol_state.reserved -= notional;
}

void CapitalAllocatorV2::update_engine_weights(double hft_weight, double struct_weight) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dynamic_hft_weight = hft_weight;
    m_dynamic_structure_weight = struct_weight;
}

double CapitalAllocatorV2::get_global_exposure() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_global_exposure.committed + m_global_exposure.reserved;
}

double CapitalAllocatorV2::get_symbol_exposure(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_symbol_exposure[symbol].committed + m_symbol_exposure[symbol].reserved;
}

double CapitalAllocatorV2::get_engine_exposure(core::EngineType engine) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_engine_exposure[engine].committed + m_engine_exposure[engine].reserved;
}

} // namespace risk
} // namespace chimera
