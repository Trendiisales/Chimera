#include "metrics/MetricsExporter.hpp"
#include <thread>
#include <chrono>

using namespace Chimera;

int main() {
    MetricsExporter exporter("metrics_out/chimera_metrics");

    while (true) {
        exporter.export_csv();
        exporter.export_json();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
