#include "RegimeStore.hpp"

namespace chimera_lab {

RegimeStore::RegimeStore(const std::string& file) {
    out.open(file, std::ios::app);
    out << "trade_id,symbol,regime,ofi,impulse,spread,depth,toxic,vpin,funding,regime_contrib,total_pnl\n";
}

void RegimeStore::write(uint64_t trade_id,
                         const std::string& symbol,
                         const std::string& regime,
                         const AttributionResult& r,
                         double total_pnl) {
    out << trade_id << ","
        << symbol << ","
        << regime << ","
        << r.ofi << ","
        << r.impulse << ","
        << r.spread << ","
        << r.depth << ","
        << r.toxic << ","
        << r.vpin << ","
        << r.funding << ","
        << r.regime << ","
        << total_pnl << "\n";
    out.flush();
}

} // namespace chimera_lab
