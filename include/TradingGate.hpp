#pragma once
#include <atomic>
#include <string>

class TradingGate {
public:
    TradingGate() : enabled_(false) {}

    void enable(const std::string& reason) {
        last_reason_ = reason;
        enabled_.store(true, std::memory_order_release);
    }

    void disable(const std::string& reason) {
        last_reason_ = reason;
        enabled_.store(false, std::memory_order_release);
    }

    bool is_enabled() const {
        return enabled_.load(std::memory_order_acquire);
    }

    const std::string& last_reason() const {
        return last_reason_;
    }

private:
    std::atomic<bool> enabled_;
    std::string last_reason_;
};
