#pragma once

#include "../risk/CapitalAllocatorV3.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>

namespace chimera {
namespace core {

// FIX #8: Periodic sanity check prevents silent corruption
class AllocatorAuditor {
public:
    explicit AllocatorAuditor(risk::CapitalAllocatorV3& allocator);
    ~AllocatorAuditor();
    
    void start();
    void stop();
    
    bool has_detected_corruption() const { return m_corruption_detected.load(); }

private:
    void audit_loop();
    
    risk::CapitalAllocatorV3& m_allocator;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_corruption_detected{false};
    std::thread m_thread;
};

} // namespace core
} // namespace chimera
