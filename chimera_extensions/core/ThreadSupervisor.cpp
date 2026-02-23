#include "ThreadSupervisor.hpp"

namespace chimera {
namespace core {

void ThreadSupervisor::safe_thread_execution(const std::string& thread_name,
                                            std::function<void()> fn,
                                            std::atomic<bool>& engine_disabled) {
    try {
        // GAP #5 FIX: Wrap execution in try-catch
        fn();
    } 
    catch (const std::exception& e) {
        std::cerr << "⚠️ THREAD FAILURE [" << thread_name << "]: " 
                  << e.what() << "\n";
        engine_disabled.store(true);
    }
    catch (...) {
        std::cerr << "⚠️ THREAD FAILURE [" << thread_name << "]: Unknown exception\n";
        engine_disabled.store(true);
    }
}

} // namespace core
} // namespace chimera
