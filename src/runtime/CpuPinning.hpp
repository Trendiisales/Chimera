#pragma once
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <thread>

namespace chimera {

class CpuPinning {
public:
    static void pin_thread(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_t current = pthread_self();
        if (pthread_setaffinity_np(current, sizeof(cpu_set_t), &cpuset) != 0)
            throw std::runtime_error("Failed to set CPU affinity");
    }

    static unsigned int cores() {
        return std::thread::hardware_concurrency();
    }
};

}
