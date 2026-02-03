#include "control/EdgeMonitor.hpp"
#include <sstream>

namespace chimera {

EdgeMonitor::EdgeMonitor(EventJournal& journal)
    : m_journal(journal) {}

void EdgeMonitor::onLatency(const std::string& engine, double ns) {
    m_latency[engine] = ns;
}

bool EdgeMonitor::allow(const std::string& engine) {
    auto it = m_latency.find(engine);
    if (it == m_latency.end()) return true;

    if (it->second > 5000000.0) {
        std::ostringstream p;
        p << "{"
          << "\"engine\":\"" << engine << "\","
          << "\"latency_ns\":" << it->second
          << "}";
        m_journal.write("EDGE_DECAY", p.str());
        return false;
    }
    return true;
}

}
