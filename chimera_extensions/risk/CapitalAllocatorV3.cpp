#include "CapitalAllocatorV3.hpp"
#include <algorithm>

namespace chimera {
namespace risk {

CapitalAllocatorV3::CapitalAllocatorV3(double global_cap) : m_global_cap(global_cap) {}

double CapitalAllocatorV3::calculate_notional(const core::OrderIntent& intent) {
    return intent.quantity * intent.price;
}

bool CapitalAllocatorV3::reserve(const core::OrderIntent& intent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    double notional = calculate_notional(intent);
    
    // FIX #2: Bounded checks with floor/ceiling
    double projected_global = m_global_exposure.reserved + m_global_exposure.committed + notional;
    if (projected_global > m_global_cap)
        return false;
    
    double engine_limit = (intent.engine == core::EngineType::HFT) 
        ? m_global_cap * std::clamp(m_dynamic_hft_weight, 0.2, 0.8)
        : m_global_cap * std::clamp(m_dynamic_structure_weight, 0.2, 0.8);
    
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

void CapitalAllocatorV3::commit(const core::OrderIntent& intent) {
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

// FIX #1: CRITICAL - Partial fill reconciliation
void CapitalAllocatorV3::adjust_on_fill(const core::OrderIntent& intent,
                                        double actual_fill_qty,
                                        double actual_fill_price) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    double reserved_notional = intent.quantity * intent.price;
    double actual_notional = actual_fill_qty * actual_fill_price;
    double remainder = reserved_notional - actual_notional;
    
    // Move from reserved to committed based on actual fill
    m_global_exposure.reserved -= reserved_notional;
    m_global_exposure.committed += actual_notional;
    
    auto& engine_state = m_engine_exposure[intent.engine];
    engine_state.reserved -= reserved_notional;
    engine_state.committed += actual_notional;
    
    auto& symbol_state = m_symbol_exposure[intent.symbol];
    symbol_state.reserved -= reserved_notional;
    symbol_state.committed += actual_notional;
    
    // Release unfilled portion
    if (remainder > 0.0) {
        m_global_exposure.committed -= remainder;
        engine_state.committed -= remainder;
        symbol_state.committed -= remainder;
    }
}

void CapitalAllocatorV3::release(const core::OrderIntent& intent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    double notional = calculate_notional(intent);
    
    m_global_exposure.reserved -= notional;
    
    auto& engine_state = m_engine_exposure[intent.engine];
    engine_state.reserved -= notional;
    
    auto& symbol_state = m_symbol_exposure[intent.symbol];
    symbol_state.reserved -= notional;
}

void CapitalAllocatorV3::update_engine_weights(double hft_weight, double struct_weight) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dynamic_hft_weight = hft_weight;
    m_dynamic_structure_weight = struct_weight;
}

double CapitalAllocatorV3::get_global_exposure() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_global_exposure.committed + m_global_exposure.reserved;
}

double CapitalAllocatorV3::get_symbol_exposure(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_symbol_exposure[symbol].committed + m_symbol_exposure[symbol].reserved;
}

double CapitalAllocatorV3::get_engine_exposure(core::EngineType engine) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_engine_exposure[engine].committed + m_engine_exposure[engine].reserved;
}

// FIX #5: Get NET exposure for hedge direction
double CapitalAllocatorV3::get_net_exposure(core::EngineType engine) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_engine_exposure[engine].committed;
}

} // namespace risk
} // namespace chimera
