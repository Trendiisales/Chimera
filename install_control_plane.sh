#!/usr/bin/env bash
set -euo pipefail

BASE="$PWD/src/core/control"
STATE="$PWD/src/core/state"
LAT="$PWD/src/core/latency"

mkdir -p "$BASE"

############################################
# CONTROL PLANE
############################################
cat > "$BASE/ControlPlane.hpp" << 'HPP'
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
HPP

cat > "$BASE/ControlPlane.cpp" << 'CPP'
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
CPP

############################################
# STRATEGY ARBITER
############################################
cat > "$BASE/StrategyArbiter.hpp" << 'HPP'
#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include "state/EventJournal.hpp"

namespace chimera {

class StrategyArbiter {
public:
    explicit StrategyArbiter(EventJournal& journal);
    bool allow(const std::string& engine,
               const std::string& symbol);

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, uint64_t> m_last_trade;
};

}
HPP

cat > "$BASE/StrategyArbiter.cpp" << 'CPP'
#include "control/StrategyArbiter.hpp"
#include <sstream>
#include <chrono>

namespace chimera {

static uint64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

StrategyArbiter::StrategyArbiter(EventJournal& journal)
    : m_journal(journal) {}

bool StrategyArbiter::allow(const std::string& engine,
                            const std::string& symbol) {
    std::string key = engine + ":" + symbol;
    uint64_t t = nowNs();

    auto it = m_last_trade.find(key);
    if (it != m_last_trade.end()) {
        if (t - it->second < 1000000) {
            std::ostringstream p;
            p << "{"
              << "\"engine\":\"" << engine << "\","
              << "\"symbol\":\"" << symbol << "\","
              << "\"reason\":\"COOLDOWN\""
              << "}";
            m_journal.write("ENGINE_THROTTLED", p.str());
            return false;
        }
    }

    m_last_trade[key] = t;
    return true;
}

}
CPP

############################################
# REGIME SUPERVISOR
############################################
cat > "$BASE/RegimeSupervisor.hpp" << 'HPP'
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
HPP

cat > "$BASE/RegimeSupervisor.cpp" << 'CPP'
#include "control/RegimeSupervisor.hpp"
#include <sstream>

namespace chimera {

RegimeSupervisor::RegimeSupervisor(EventJournal& journal)
    : m_journal(journal),
      m_regime(TREND) {}

void RegimeSupervisor::set(Regime r) {
    m_regime = r;
    std::ostringstream p;
    p << "{"
      << "\"regime\":" << m_regime
      << "}";
    m_journal.write("REGIME_CHANGE", p.str());
}

bool RegimeSupervisor::allow(const std::string& engine) {
    if (m_regime == CHAOS) return false;
    if (m_regime == RANGE && engine == "BTCascade") return false;
    return true;
}

}
CPP

############################################
# RISK GOVERNOR
############################################
cat > "$BASE/RiskGovernor.hpp" << 'HPP'
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
HPP

cat > "$BASE/RiskGovernor.cpp" << 'CPP'
#include "control/RiskGovernor.hpp"

namespace chimera {

RiskGovernor::RiskGovernor(EventJournal& journal)
    : m_journal(journal),
      m_frozen(false) {}

bool RiskGovernor::allowGlobal() {
    return !m_frozen.load();
}

void RiskGovernor::freeze() {
    m_frozen.store(true);
    m_journal.write("GLOBAL_FREEZE", "{}");
}

}
CPP

############################################
# CAPITAL ALLOCATOR
############################################
cat > "$BASE/CapitalAllocator.hpp" << 'HPP'
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
HPP

cat > "$BASE/CapitalAllocator.cpp" << 'CPP'
#include "control/CapitalAllocator.hpp"
#include <sstream>

namespace chimera {

CapitalAllocator::CapitalAllocator(EventJournal& journal)
    : m_journal(journal) {}

double CapitalAllocator::multiplier(const std::string& engine) {
    auto it = m_mult.find(engine);
    if (it == m_mult.end()) return 1.0;
    return it->second;
}

void CapitalAllocator::set(const std::string& engine, double mult) {
    m_mult[engine] = mult;
    std::ostringstream p;
    p << "{"
      << "\"engine\":\"" << engine << "\","
      << "\"multiplier\":" << mult
      << "}";
    m_journal.write("CAPITAL_REALLOCATED", p.str());
}

}
CPP

############################################
# EDGE MONITOR
############################################
cat > "$BASE/EdgeMonitor.hpp" << 'HPP'
#pragma once
#include <string>
#include <unordered_map>
#include "state/EventJournal.hpp"

namespace chimera {

class EdgeMonitor {
public:
    explicit EdgeMonitor(EventJournal& journal);

    void onLatency(const std::string& engine, double ns);
    bool allow(const std::string& engine);

private:
    EventJournal& m_journal;
    std::unordered_map<std::string, double> m_latency;
};

}
HPP

cat > "$BASE/EdgeMonitor.cpp" << 'CPP'
#include "control/EdgeMonitor.hpp"
#include <sstream>

namespace chimera {

EdgeMonitor::EdgeMonitor(EventJournal& journal)
    : m_journal(journal) {}

void EdgeMonitor::onLatency(const std::string& engine, double ns) {
    m_latency[engine] = ns;
}

bool EdgeMonitor::allow(const std::string& engine) {
    auto it = m_latency.find(engine);
    if (it == m_latency.end()) return true;

    if (it->second > 5000000.0) {
        std::ostringstream p;
        p << "{"
          << "\"engine\":\"" << engine << "\","
          << "\"latency_ns\":" << it->second
          << "}";
        m_journal.write("EDGE_DECAY", p.str());
        return false;
    }
    return true;
}

}
CPP

############################################
# VENUE HEALTH
############################################
cat > "$BASE/VenueHealth.hpp" << 'HPP'
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
HPP

cat > "$BASE/VenueHealth.cpp" << 'CPP'
#include "control/VenueHealth.hpp"
#include <sstream>

namespace chimera {

VenueHealth::VenueHealth(EventJournal& journal)
    : m_journal(journal) {}

void VenueHealth::update(const std::string& venue, int state) {
    m_state[venue] = state;
    std::ostringstream p;
    p << "{"
      << "\"venue\":\"" << venue << "\","
      << "\"state\":" << state
      << "}";
    m_journal.write("VENUE_STATE", p.str());
}

int VenueHealth::state(const std::string& venue) const {
    auto it = m_state.find(venue);
    if (it == m_state.end()) return 0;
    return it->second;
}

}
CPP

echo "[CHIMERA] CONTROL PLANE INSTALLED"
echo "PATH: src/core/control"
