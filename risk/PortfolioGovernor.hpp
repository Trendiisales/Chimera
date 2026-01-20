#pragma once

#include <atomic>
#include <string>

class PortfolioGovernor {
public:
    PortfolioGovernor() = default;

    // GLOBAL KILL STATE
    inline bool isKilled() const {
        return killed_.load(std::memory_order_relaxed);
    }

    inline void kill(const std::string& reason = "") {
        (void)reason;
        killed_.store(true, std::memory_order_relaxed);
    }

    inline void reset() {
        killed_.store(false, std::memory_order_relaxed);
    }

    // POSITION / RISK GATES (CORE MODE STUB)
    inline bool allowOrder(
        const std::string&,
        double,
        bool,
        double
    ) const {
        return !isKilled();
    }

private:
    std::atomic<bool> killed_{false};
};
