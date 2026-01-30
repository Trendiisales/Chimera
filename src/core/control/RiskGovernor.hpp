#pragma once
#include <atomic>
#include "state/EventJournal.hpp"

namespace chimera {

class RiskGovernor {
public:
    explicit RiskGovernor(EventJournal& journal);

    bool allowGlobal();
    void freeze();

private:
    EventJournal& m_journal;
    std::atomic<bool> m_frozen;
};

}
