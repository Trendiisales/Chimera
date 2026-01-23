#pragma once
#include "chimera/causal_lab/AttributionEngine.hpp"
#include <fstream>
#include <string>

namespace chimera_lab {

class RegimeStore {
public:
    explicit RegimeStore(const std::string& file);

    void write(uint64_t trade_id,
               const std::string& symbol,
               const std::string& regime,
               const AttributionResult& r,
               double total_pnl);

private:
    std::ofstream out;
};

} // namespace chimera_lab
