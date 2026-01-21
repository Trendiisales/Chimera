#include "chimera/infra/RateLimitGovernor.hpp"

namespace chimera {

void RateLimitGovernor::onResponse(
    int w,
    int l
) {
    used.store(w);
    max.store(l);
}

bool RateLimitGovernor::allow() const {
    return used.load() < max.load() * 0.9;
}

}
