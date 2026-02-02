#include "control/ControlPlane.hpp"
#include <sstream>

namespace chimera {

ControlPlane::ControlPlane(PositionState& ps,
                           EventJournal& journal)
    : m_positions(ps),
      m_journal(journal),
      m_arbiter(journal),
      m_regime(journal),
      m_risk(journal),
      m_capital(journal),
      m_edge(journal),
      m_venue(journal) {}

ControlDecision ControlPlane::evaluate(const std::string& engine,
                                       const std::string& symbol,
                                       double,
                                       double,
                                       uint64_t event_id) {
    ControlDecision out;

    if (!m_risk.allowGlobal()) {
        out.allowed = false;
        out.reason = "GLOBAL_RISK_FREEZE";
        return out;
    }

    if (!m_regime.allow(engine)) {
        out.allowed = false;
        out.reason = "REGIME_BLOCK";
        return out;
    }

    if (!m_edge.allow(engine)) {
        out.allowed = false;
        out.reason = "EDGE_DECAY";
        return out;
    }

    if (!m_arbiter.allow(engine, symbol)) {
        out.allowed = false;
        out.reason = "ARBITER_CONFLICT";
        return out;
    }

    double cap = m_capital.multiplier(engine);
    out.allowed = true;
    out.size_mult = cap;
    out.reason = "ALLOWED";

    std::ostringstream p;
    p << "{"
      << "\"engine\":\"" << engine << "\","
      << "\"symbol\":\"" << symbol << "\","
      << "\"multiplier\":" << cap << ","
      << "\"decision\":\"ALLOWED\""
      << "}";

    m_journal.write("CONTROL_DECISION", p.str(), event_id);
    return out;
}

void ControlPlane::onLatencySample(const std::string& engine, double ns) {
    m_edge.onLatency(engine, ns);
}

void ControlPlane::onVenueHealth(const std::string& venue, int state) {
    m_venue.update(venue, state);
}

}
