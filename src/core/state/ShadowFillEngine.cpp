#include "state/ShadowFillEngine.hpp"

namespace chimera {

ShadowFillEngine::ShadowFillEngine(PositionState& ps, EventJournal& journal)
    : m_positions(ps), m_journal(journal) {}

void ShadowFillEngine::onOrderIntent(const std::string& symbol,
                                     const std::string& engine_id,
                                     double price,
                                     double qty) {
    uint64_t eid = m_journal.nextEventId();

    m_positions.onFill(symbol, engine_id, price, qty, 0.0, eid);

    m_journal.write("SHADOW_FILL",
                     "{\"symbol\":\"" + symbol +
                     "\",\"engine\":\"" + engine_id +
                     "\",\"price\":" + std::to_string(price) +
                     ",\"qty\":" + std::to_string(qty) + "}",
                     eid);
}

}
