#include "control/StrategyArbiter.hpp"
#include <sstream>
#include <chrono>

namespace chimera {

static uint64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

StrategyArbiter::StrategyArbiter(EventJournal& journal)
    : m_journal(journal) {}

bool StrategyArbiter::allow(const std::string& engine,
                            const std::string& symbol) {
    std::string key = engine + ":" + symbol;
    uint64_t t = nowNs();

    auto it = m_last_trade.find(key);
    if (it != m_last_trade.end()) {
        if (t - it->second < 1000000) {
            std::ostringstream p;
            p << "{"
              << "\"engine\":\"" << engine << "\","
              << "\"symbol\":\"" << symbol << "\","
              << "\"reason\":\"COOLDOWN\""
              << "}";
            m_journal.write("ENGINE_THROTTLED", p.str());
            return false;
        }
    }

    m_last_trade[key] = t;
    return true;
}

}
