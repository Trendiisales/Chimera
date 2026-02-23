#pragma once

#include "../core/OrderIntentTypes.hpp"
#include <mutex>
#include <unordered_map>

namespace chimera {
namespace risk {

struct ExposureState {
    double committed = 0.0;
    double reserved = 0.0;
};

class CapitalAllocatorV3 {
public:
    explicit CapitalAllocatorV3(double global_cap);

    bool reserve(const core::OrderIntent& intent);
    void commit(const core::OrderIntent& intent);
    void release(const core::OrderIntent& intent);
    
    // FIX #1: Partial fill reconciliation
    void adjust_on_fill(const core::OrderIntent& intent, 
                       double actual_fill_qty, 
                       double actual_fill_price);
    
    void update_engine_weights(double hft_weight, double struct_weight);

    double get_global_exposure();
    double get_symbol_exposure(const std::string& symbol);
    double get_engine_exposure(core::EngineType engine);
    double get_net_exposure(core::EngineType engine); // FIX #5: Hedge direction

private:
    double calculate_notional(const core::OrderIntent& intent);

    double m_global_cap;
    double m_dynamic_hft_weight = 0.6;
    double m_dynamic_structure_weight = 0.4;

    std::unordered_map<std::string, ExposureState> m_symbol_exposure;
    std::unordered_map<core::EngineType, ExposureState> m_engine_exposure;
    ExposureState m_global_exposure;

    std::mutex m_mutex;
};

} // namespace risk
} // namespace chimera
