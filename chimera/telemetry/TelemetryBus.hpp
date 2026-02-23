#pragma once
#include <vector>
#include <mutex>
#include "TelemetrySink.hpp"

namespace chimera {

class TelemetryBus {
public:
    void subscribe(TelemetrySink* sink);
    void publish(const TelemetryEvent& ev);

private:
    std::vector<TelemetrySink*> m_sinks;
    std::mutex m_lock;
};

}
