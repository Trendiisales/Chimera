#include "control/RegimeSupervisor.hpp"
#include <sstream>

namespace chimera {

RegimeSupervisor::RegimeSupervisor(EventJournal& journal)
    : m_journal(journal),
      m_regime(TREND) {}

void RegimeSupervisor::set(Regime r) {
    m_regime = r;
    std::ostringstream p;
    p << "{"
      << "\"regime\":" << m_regime
      << "}";
    m_journal.write("REGIME_CHANGE", p.str());
}

bool RegimeSupervisor::allow(const std::string& engine) {
    if (m_regime == CHAOS) return false;
    if (m_regime == RANGE && engine == "BTCascade") return false;
    return true;
}

}
