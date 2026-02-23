#pragma once

#include <functional>
#include <string>
#include <atomic>
#include <iostream>

namespace chimera {
namespace core {

// GAP #5 FIX: Prevents single engine crash from killing system
class ThreadSupervisor {
public:
    static void safe_thread_execution(const std::string& thread_name,
                                     std::function<void()> fn,
                                     std::atomic<bool>& engine_disabled);
};

} // namespace core
} // namespace chimera
