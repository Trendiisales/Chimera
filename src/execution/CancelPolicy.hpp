#pragma once
#include <cstdint>

namespace chimera {

class CancelPolicy {
public:
    CancelPolicy(uint64_t max_wait_ns, double min_fill_prob)
        : max_wait_ns_(max_wait_ns), min_fill_prob_(min_fill_prob) {}

    bool should_cancel(uint64_t now_ns, uint64_t order_ts_ns,
                       double expected_fill_prob) const {
        if (now_ns - order_ts_ns > max_wait_ns_) return true;
        return expected_fill_prob < min_fill_prob_;
    }

private:
    uint64_t max_wait_ns_;
    double min_fill_prob_;
};

}
