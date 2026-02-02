#pragma once
#include <string>
#include <cstdint>

#include "state/ShadowFillEngine.hpp"
#include "state/PositionState.hpp"
#include "state/EventJournal.hpp"

#include "control/StrategyArbiter.hpp"
#include "control/RegimeSupervisor.hpp"
#include "control/RiskGovernor.hpp"
#include "control/CapitalAllocator.hpp"
#include "control/EdgeMonitor.hpp"
#include "control/VenueHealth.hpp"

namespace chimera {

struct ControlDecision {
    bool allowed = false;
    double size_mult = 0.0;
    std::string reason;
};

class ControlPlane {
public:
    ControlPlane(PositionState& ps,
                 EventJournal& journal);

    ControlDecision evaluate(const std::string& engine,
                             const std::string& symbol,
                             double price,
                             double qty,
                             uint64_t event_id);

    void onLatencySample(const std::string& engine, double ns);
    void onVenueHealth(const std::string& venue, int state);

private:
    PositionState& m_positions;
    EventJournal& m_journal;

    StrategyArbiter m_arbiter;
    RegimeSupervisor m_regime;
    RiskGovernor m_risk;
    CapitalAllocator m_capital;
    EdgeMonitor m_edge;
    VenueHealth m_venue;
};

}
