#pragma once
#include <string>
#include <unordered_map>
#include "state/EventJournal.hpp"

namespace chimera {

class VenueHealth {
public:
    explicit VenueHealth(EventJournal& journal);

    void update(const std::string& venue, int state);
    int state(const std::string& venue) const;

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, int> m_state;
};

}
