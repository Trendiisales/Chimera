#pragma once
#include <string>
#include <cstdint>

#include "state/EventJournal.hpp"

namespace chimera {

class VenueRouter {
public:
    explicit VenueRouter(EventJournal& journal);

    void sendOrder(const std::string& venue,
                   const std::string& symbol,
                   double price,
                   double qty,
                   bool taker,
                   uint64_t event_id);

private:
    EventJournal& m_journal;
};

}
