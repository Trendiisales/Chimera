#include "control/VenueHealth.hpp"
#include <sstream>

namespace chimera {

VenueHealth::VenueHealth(EventJournal& journal)
    : m_journal(journal) {}

void VenueHealth::update(const std::string& venue, int state) {
    m_state[venue] = state;
    std::ostringstream p;
    p << "{"
      << "\"venue\":\"" << venue << "\","
      << "\"state\":" << state
      << "}";
    m_journal.write("VENUE_STATE", p.str());
}

int VenueHealth::state(const std::string& venue) const {
    auto it = m_state.find(venue);
    if (it == m_state.end()) return 0;
    return it->second;
}

}
