#include "TelemetrySinkStdout.hpp"

namespace chimera {

void TelemetrySinkStdout::publish(const TelemetryEvent& ev) {
    std::cout
        << "[TELEMETRY]"
        << " type=" << static_cast<int>(ev.type)
        << " ts=" << ev.ts_ns
        << " payload=" << ev.payload_json
        << "\n";
}

}
