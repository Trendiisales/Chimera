#pragma once
#include <atomic>
#include <cstdint>

namespace Chimera {

struct alignas(64) VenueSnapshot {
    bool healthy;
    bool throttled;
    double size_mult;
};

class ExecutionArbiter {
public:
    ExecutionArbiter();

    void update_binance(bool blind_active);
    void update_fix(bool allow_orders, double size_mult);

    bool allow_execution() const;
    double size_multiplier() const;

private:
    std::atomic<bool> binance_ok_;
    std::atomic<bool> fix_ok_;
    std::atomic<double> fix_size_mult_;
};

}
