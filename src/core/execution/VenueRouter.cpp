#include "execution/VenueRouter.hpp"
#include <sstream>

namespace chimera {

VenueRouter::VenueRouter(EventJournal& journal)
    : m_journal(journal) {}

void VenueRouter::sendOrder(const std::string& venue,
                            const std::string& symbol,
                            double price,
                            double qty,
                            bool taker,
                            uint64_t event_id) {
    std::ostringstream p;
    p << "{"
      << "\"venue\":\"" << venue << "\","
      << "\"symbol\":\"" << symbol << "\","
      << "\"price\":" << price << ","
      << "\"qty\":" << qty << ","
      << "\"mode\":\"" << (taker ? "TAKER" : "MAKER") << "\""
      << "}";

    m_journal.write("ORDER_ROUTED", p.str(), event_id);
}

}
