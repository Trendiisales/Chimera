#pragma once
#include <atomic>

class MetricsServer {
public:
    void inc() { count_.fetch_add(1, std::memory_order_relaxed); }
    unsigned long long value() const { return count_.load(std::memory_order_relaxed); }
private:
    std::atomic<unsigned long long> count_{0};
};
