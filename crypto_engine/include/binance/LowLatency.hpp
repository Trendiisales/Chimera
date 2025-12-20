#pragma once
#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace binance {

/* Cache-line size (safe default) */
constexpr std::size_t CACHELINE = 64;

/* Align hot structs */
#define CACHE_ALIGN alignas(CACHELINE)

/*
 Best-effort CPU pinning.
 - macOS: no hard pinning without entitlements → no-op
 - Linux: pthread_setaffinity_np
*/
inline void pin_thread(int cpu) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)cpu;
#endif
}

}
