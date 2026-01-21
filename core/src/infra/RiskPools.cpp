#include "chimera/infra/RiskPools.hpp"
#include <cmath>

namespace chimera {

void RiskPools::set(
    const std::string& sym,
    double max_exp
) {
    pools[sym].max_exposure = max_exp;
}

bool RiskPools::allow(
    const std::string& sym,
    double qty
) {
    auto& p = pools[sym];
    return std::abs(p.current + qty)
           <= p.max_exposure;
}

void RiskPools::onFill(
    const std::string& sym,
    double delta
) {
    pools[sym].current += delta;
}

}
