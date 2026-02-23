#include "AllocatorAuditor.hpp"
#include <iostream>

namespace chimera {
namespace core {

AllocatorAuditor::AllocatorAuditor(risk::CapitalAllocatorV3& allocator)
    : m_allocator(allocator) {}

AllocatorAuditor::~AllocatorAuditor() {
    if (m_running.load())
        stop();
}

void AllocatorAuditor::start() {
    m_running.store(true);
    m_thread = std::thread(&AllocatorAuditor::audit_loop, this);
}

void AllocatorAuditor::stop() {
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.join();
}

void AllocatorAuditor::audit_loop() {
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        
        // FIX #8: Verify engine exposures sum to global
        double hft_exp = m_allocator.get_engine_exposure(EngineType::HFT);
        double struct_exp = m_allocator.get_engine_exposure(EngineType::STRUCTURE);
        double global_exp = m_allocator.get_global_exposure();
        
        double engine_sum = hft_exp + struct_exp;
        double diff = std::abs(engine_sum - global_exp);
        
        if (diff > 1.0) {  // Tolerance: $1
            m_corruption_detected.store(true);
            std::cerr << "⚠️ ALLOCATOR AUDIT FAILURE - Exposure mismatch: "
                      << "Engines=" << engine_sum 
                      << " Global=" << global_exp
                      << " Diff=" << diff << "\n";
        }
        
        // Check for negative exposure (should never happen)
        if (global_exp < -0.01 || hft_exp < -0.01 || struct_exp < -0.01) {
            m_corruption_detected.store(true);
            std::cerr << "⚠️ ALLOCATOR AUDIT FAILURE - Negative exposure detected\n";
        }
    }
}

} // namespace core
} // namespace chimera
