#include "control/CapitalAllocator.hpp"
#include <sstream>

namespace chimera {

CapitalAllocator::CapitalAllocator(EventJournal& journal)
    : m_journal(journal) {}

double CapitalAllocator::multiplier(const std::string& engine) {
    auto it = m_mult.find(engine);
    if (it == m_mult.end()) return 1.0;
    return it->second;
}

void CapitalAllocator::set(const std::string& engine, double mult) {
    m_mult[engine] = mult;
    std::ostringstream p;
    p << "{"
      << "\"engine\":\"" << engine << "\","
      << "\"multiplier\":" << mult
      << "}";
    m_journal.write("CAPITAL_REALLOCATED", p.str());
}

}
