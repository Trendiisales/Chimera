#pragma once
#include "chimera/infra/RateLimitGovernor.hpp"

namespace chimera::venue {

class RateGuard {
public:
    RateGuard(RateLimitGovernor& r) : rate(r) {}

    bool allow() {
        return rate.allow();
    }

    void onResponse(uint64_t used, uint64_t limit) {
        rate.onResponse(used, limit);
    }

private:
    RateLimitGovernor& rate;
};

}
