#pragma once
// =============================================================================
// ThreadPinning.hpp v4.2.2 - Hard CPU Isolation & Realtime Scheduling
// =============================================================================
// Guarantees that search, execution, risk, metrics, and HTTP threads
// can NEVER steal time from each other.
//
// CRITICAL: Search thread must NEVER share a core with anything.
//
// Canonical layout (8 cores):
//   CPU 0 → Search / Strategy (HOT PATH)
//   CPU 1 → Execution (FIX / Venue)
//   CPU 2 → Risk + Kill-switch
//   CPU 3 → Metrics Snapshot Producer
//   CPU 4 → HTTP Server
//   CPU 5-7 → OS / idle / interrupts
// =============================================================================

#include <thread>
#include <cstdint>
#include <iostream>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace Omega {

// =============================================================================
// THREAD ROLE ENUM - Canonical CPU assignments
// =============================================================================
enum class ThreadRole : uint8_t {
    SEARCH = 0,      // CPU 0 - HOT PATH, highest priority
    EXECUTION = 1,   // CPU 1 - FIX/Venue I/O
    RISK = 2,        // CPU 2 - Kill-switch, risk checks
    METRICS = 3,     // CPU 3 - Snapshot producer
    HTTP = 4,        // CPU 4 - Dashboard server
    LOGGING = 5,     // CPU 5 - Async logging
    OS_TASKS = 6     // CPU 6-7 - OS background
};

inline const char* ThreadRoleStr(ThreadRole role) {
    switch (role) {
        case ThreadRole::SEARCH: return "SEARCH";
        case ThreadRole::EXECUTION: return "EXECUTION";
        case ThreadRole::RISK: return "RISK";
        case ThreadRole::METRICS: return "METRICS";
        case ThreadRole::HTTP: return "HTTP";
        case ThreadRole::LOGGING: return "LOGGING";
        case ThreadRole::OS_TASKS: return "OS_TASKS";
    }
    return "UNKNOWN";
}

// =============================================================================
// CPU PINNING - Lock thread to specific core
// =============================================================================
#ifdef __linux__

inline bool PinThreadToCPU(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (result == 0) {
        std::cout << "[THREAD-PIN] Pinned to CPU " << cpu << "\n";
        return true;
    }
    std::cerr << "[THREAD-PIN] Failed to pin to CPU " << cpu << ": " << result << "\n";
    return false;
}

inline bool PinThreadToCPU(std::thread& t, int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    int result = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    return result == 0;
}

inline bool SetRealtimePriority(int priority) {
    sched_param sp{};
    sp.sched_priority = priority;
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (result == 0) {
        std::cout << "[THREAD-PIN] Set SCHED_FIFO priority " << priority << "\n";
        return true;
    }
    // Fall back to RR if FIFO fails (needs root)
    result = pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
    if (result == 0) {
        std::cout << "[THREAD-PIN] Set SCHED_RR priority " << priority << "\n";
        return true;
    }
    std::cerr << "[THREAD-PIN] Failed to set RT priority: " << result << "\n";
    return false;
}

inline int GetCPUCount() {
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

#elif defined(_WIN32)

inline bool PinThreadToCPU(int cpu) {
    DWORD_PTR mask = 1ULL << cpu;
    DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), mask);
    if (result != 0) {
        std::cout << "[THREAD-PIN] Pinned to CPU " << cpu << "\n";
        return true;
    }
    std::cerr << "[THREAD-PIN] Failed to pin to CPU " << cpu << "\n";
    return false;
}

inline bool PinThreadToCPU(std::thread& t, int cpu) {
    DWORD_PTR mask = 1ULL << cpu;
    DWORD_PTR result = SetThreadAffinityMask(t.native_handle(), mask);
    return result != 0;
}

inline bool SetRealtimePriority(int priority) {
    // Map priority 1-99 to Windows priority classes
    int win_priority = THREAD_PRIORITY_NORMAL;
    if (priority >= 80) win_priority = THREAD_PRIORITY_TIME_CRITICAL;
    else if (priority >= 60) win_priority = THREAD_PRIORITY_HIGHEST;
    else if (priority >= 40) win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
    
    BOOL result = SetThreadPriority(GetCurrentThread(), win_priority);
    if (result) {
        std::cout << "[THREAD-PIN] Set thread priority " << win_priority << "\n";
        return true;
    }
    std::cerr << "[THREAD-PIN] Failed to set priority\n";
    return false;
}

inline int GetCPUCount() {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return static_cast<int>(sysinfo.dwNumberOfProcessors);
}

#else

// Fallback for unsupported platforms
inline bool PinThreadToCPU(int) { return false; }
inline bool PinThreadToCPU(std::thread&, int) { return false; }
inline bool SetRealtimePriority(int) { return false; }
inline int GetCPUCount() { return 1; }

#endif

// =============================================================================
// THREAD SETUP - Apply role-specific pinning and priority
// =============================================================================
inline void SetupThreadForRole(ThreadRole role) {
    int cpu_count = GetCPUCount();
    int cpu = static_cast<int>(role);
    
    // Clamp to available CPUs
    if (cpu >= cpu_count) {
        cpu = cpu % cpu_count;
    }
    
    // Pin to CPU
    PinThreadToCPU(cpu);
    
    // Set priority based on role
    int priority = 50;  // Default
    switch (role) {
        case ThreadRole::SEARCH:    priority = 80; break;
        case ThreadRole::EXECUTION: priority = 70; break;
        case ThreadRole::RISK:      priority = 60; break;
        case ThreadRole::METRICS:   priority = 40; break;
        case ThreadRole::HTTP:      priority = 30; break;
        case ThreadRole::LOGGING:   priority = 20; break;
        default: break;
    }
    
    SetRealtimePriority(priority);
    
    std::cout << "[THREAD-PIN] Thread role=" << ThreadRoleStr(role) 
              << " cpu=" << cpu << " priority=" << priority << "\n";
}

// =============================================================================
// THREAD GUARD - RAII wrapper for thread setup
// =============================================================================
class ThreadGuard {
public:
    explicit ThreadGuard(ThreadRole role) {
        SetupThreadForRole(role);
    }
};

// Usage in thread entry:
// void SearchThread() {
//     ThreadGuard guard(ThreadRole::SEARCH);
//     // ... hot path ...
// }

} // namespace Omega
