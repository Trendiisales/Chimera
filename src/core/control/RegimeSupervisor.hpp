#pragma once
#include <string>
#include "state/EventJournal.hpp"

namespace chimera {

enum Regime {
    TREND,
    RANGE,
    CHAOS
};

class RegimeSupervisor {
public:
    explicit RegimeSupervisor(EventJournal& journal);

    bool allow(const std::string& engine);
    void set(Regime r);

private:
    EventJournal& m_journal;
    Regime m_regime;
};

}
