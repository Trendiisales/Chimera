#include "metrics/MetricsExporter.hpp"
#include "metrics/MetricsRegistry.hpp"
#include <fstream>

using namespace Chimera;

MetricsExporter::MetricsExporter(const std::string& prefix)
    : prefix_(prefix) {}

void MetricsExporter::export_csv() {
    MetricsSnapshot s = metrics().snapshot();

    std::ofstream f(prefix_ + ".csv", std::ios::app);
    if (!f.tellp()) {
        f << "ts_ns,binance_ticks,fix_execs,exec_allowed,exec_blocked,divergences,alerts_critical\n";
    }

    f << s.ts_ns << ","
      << s.binance_ticks << ","
      << s.fix_execs << ","
      << s.exec_allowed << ","
      << s.exec_blocked << ","
      << s.divergences << ","
      << s.alerts_critical << "\n";
}

void MetricsExporter::export_json() {
    MetricsSnapshot s = metrics().snapshot();

    std::ofstream f(prefix_ + ".json");
    f << "{\n";
    f << "  \"ts_ns\": " << s.ts_ns << ",\n";
    f << "  \"binance_ticks\": " << s.binance_ticks << ",\n";
    f << "  \"fix_execs\": " << s.fix_execs << ",\n";
    f << "  \"exec_allowed\": " << s.exec_allowed << ",\n";
    f << "  \"exec_blocked\": " << s.exec_blocked << ",\n";
    f << "  \"divergences\": " << s.divergences << ",\n";
    f << "  \"alerts_critical\": " << s.alerts_critical << "\n";
    f << "}\n";
}
