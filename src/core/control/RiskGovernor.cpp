#include "control/RiskGovernor.hpp"

namespace chimera {

RiskGovernor::RiskGovernor(EventJournal& journal)
    : m_journal(journal),
      m_frozen(false) {}

bool RiskGovernor::allowGlobal() {
    return !m_frozen.load();
}

void RiskGovernor::freeze() {
    m_frozen.store(true);
    m_journal.write("GLOBAL_FREEZE", "{}");
}

}
