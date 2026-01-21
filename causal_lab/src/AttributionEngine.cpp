#include "AttributionEngine.hpp"

namespace chimera_lab {

double AttributionEngine::contrib(double full, double partial) {
    return full - partial;
}

AttributionResult AttributionEngine::shapley(
    const std::map<std::string, double>& baseline,
    const std::map<std::string, double>& no_ofi,
    const std::map<std::string, double>& no_impulse,
    const std::map<std::string, double>& no_spread,
    const std::map<std::string, double>& no_depth,
    const std::map<std::string, double>& no_toxic,
    const std::map<std::string, double>& no_vpin,
    const std::map<std::string, double>& no_funding,
    const std::map<std::string, double>& no_regime) {

    AttributionResult r{};

    double full = baseline.at("pnl");

    r.ofi      = contrib(full, no_ofi.at("pnl"));
    r.impulse  = contrib(full, no_impulse.at("pnl"));
    r.spread   = contrib(full, no_spread.at("pnl"));
    r.depth    = contrib(full, no_depth.at("pnl"));
    r.toxic    = contrib(full, no_toxic.at("pnl"));
    r.vpin     = contrib(full, no_vpin.at("pnl"));
    r.funding  = contrib(full, no_funding.at("pnl"));
    r.regime   = contrib(full, no_regime.at("pnl"));

    return r;
}

} // namespace chimera_lab
