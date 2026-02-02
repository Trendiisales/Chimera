#pragma once
#include <string>
#include "state/PositionState.hpp"
#include "state/EventJournal.hpp"

namespace chimera {

class ShadowFillEngine {
public:
    ShadowFillEngine(PositionState& ps, EventJournal& journal);

    void onOrderIntent(const std::string& symbol,
                       const std::string& engine_id,
                       double price,
                       double qty);

private:
    PositionState& m_positions;
    EventJournal& m_journal;
};

}
