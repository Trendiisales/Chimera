#pragma once
#include "EventTypes.hpp"
#include <map>
#include <string>

namespace chimera_lab {

struct AttributionResult {
    double ofi;
    double impulse;
    double spread;
    double depth;
    double toxic;
    double vpin;
    double funding;
    double regime;
};

class AttributionEngine {
public:
    AttributionResult shapley(const std::map<std::string, double>& baseline,
                              const std::map<std::string, double>& no_ofi,
                              const std::map<std::string, double>& no_impulse,
                              const std::map<std::string, double>& no_spread,
                              const std::map<std::string, double>& no_depth,
                              const std::map<std::string, double>& no_toxic,
                              const std::map<std::string, double>& no_vpin,
                              const std::map<std::string, double>& no_funding,
                              const std::map<std::string, double>& no_regime);

private:
    double contrib(double full, double partial);
};

} // namespace chimera_lab
