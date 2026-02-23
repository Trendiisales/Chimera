#include "ReplayEngine.hpp"
#include <iostream>

namespace chimera {

ReplayEngine::ReplayEngine(const ReplayLog& log)
    : m_log(log) {}

void ReplayEngine::replay() {
    for (const ReplayEvent& e : m_log.events()) {
        std::cout
            << "[REPLAY]"
            << " ts=" << e.ts_ns
            << " id=" << e.causal_id
            << " type=" << static_cast<int>(e.type)
            << " payload=" << e.payload_json
            << "\n";
    }
}

}
