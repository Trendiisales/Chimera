#pragma once
#include <string>
#include <unordered_map>
#include "state/EventJournal.hpp"

namespace chimera {

class CapitalAllocator {
public:
    explicit CapitalAllocator(EventJournal& journal);

    double multiplier(const std::string& engine);
    void set(const std::string& engine, double mult);

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, double> m_mult;
};

}
