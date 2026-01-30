#pragma once
#include <string>
#include <unordered_map>
#include "state/EventJournal.hpp"

namespace chimera {

class EdgeMonitor {
public:
    explicit EdgeMonitor(EventJournal& journal);

    void onLatency(const std::string& engine, double ns);
    bool allow(const std::string& engine);

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, double> m_latency;
};

}
