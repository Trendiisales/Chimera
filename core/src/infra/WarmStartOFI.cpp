#include "chimera/infra/WarmStartOFI.hpp"
#include <cmath>

namespace chimera {

void WarmStartOFI::seed(
    const std::string& sym,
    double v
) {
    map[sym] = {
        v,
        std::chrono::steady_clock::now()
    };
}

double WarmStartOFI::get(
    const std::string& sym
) {
    auto it = map.find(sym);
    if (it == map.end()) return 0.0;

    auto dt =
        std::chrono::duration_cast<
            std::chrono::seconds
        >(
            std::chrono::steady_clock::now() -
            it->second.ts
        ).count();

    double decay = std::exp(
        -dt / 30.0
    );

    return it->second.value * decay;
}

}
