#include "binance/PrometheusMetrics.hpp"

#include <sstream>

namespace binance {

PrometheusMetrics& PrometheusMetrics::instance() {
    static PrometheusMetrics inst;
    return inst;
}

SymbolMetrics& PrometheusMetrics::for_symbol(const std::string& symbol) {
    return by_symbol[symbol];
}

std::string PrometheusMetrics::render() const {
    std::ostringstream out;

    out << "# TYPE binance_snapshots_total counter\n";
    out << "# TYPE binance_deltas_total counter\n";
    out << "# TYPE binance_gaps_total counter\n";
    out << "# TYPE binance_reconnects_total counter\n";
    out << "# TYPE binance_health gauge\n";

    for (const auto& it : by_symbol) {
        const std::string& sym = it.first;
        const SymbolMetrics& m = it.second;

        out << "binance_snapshots_total{symbol=\""
            << sym << "\"} " << m.snapshots.load() << "\n";

        out << "binance_deltas_total{symbol=\""
            << sym << "\"} " << m.deltas.load() << "\n";

        out << "binance_gaps_total{symbol=\""
            << sym << "\"} " << m.gaps.load() << "\n";

        out << "binance_reconnects_total{symbol=\""
            << sym << "\"} " << m.reconnects.load() << "\n";

        out << "binance_health{symbol=\""
            << sym << "\"} " << m.health.load() << "\n";
    }

    return out.str();
}

}
