#include "chimera/control/ControlPlane.hpp"

ControlPlane::ControlPlane() :
    kill_(false),
    session_allowed_(true),
    regime_quality_(1),
    capital_tier_(1),
    latency_rank_(0) {}

void ControlPlane::setKill(bool v) {
    kill_.store(v, std::memory_order_relaxed);
}

void ControlPlane::setSessionAllowed(bool v) {
    session_allowed_.store(v, std::memory_order_relaxed);
}

void ControlPlane::setRegimeQuality(int q) {
    regime_quality_.store(q, std::memory_order_relaxed);
}

void ControlPlane::setCapitalTier(int t) {
    capital_tier_.store(t, std::memory_order_relaxed);
}

void ControlPlane::setLatencyRank(int r) {
    latency_rank_.store(r, std::memory_order_relaxed);
}

ControlDecision ControlPlane::decide(const std::string&,
                                     double edge_bps,
                                     double cost_bps,
                                     double requested_size) const {
    ControlDecision d;
    d.allow = true;
    d.flags = NONE;
    d.size_multiplier = 1.0;

    if (kill_.load()) {
        d.allow = false;
        d.flags |= KILL;
        return d;
    }

    if (!session_allowed_.load()) {
        d.allow = false;
        d.flags |= SESSION;
        return d;
    }

    if (edge_bps <= cost_bps) {
        d.allow = false;
        d.flags |= COST_FAIL;
        return d;
    }

    int rq = regime_quality_.load();
    if (rq <= 0) {
        d.allow = false;
        d.flags |= REGIME;
        return d;
    }

    int tier = capital_tier_.load();
    if (tier <= 0) {
        d.allow = false;
        d.flags |= CAPITAL;
        return d;
    }

    int lat = latency_rank_.load();
    if (lat > 0) {
        d.size_multiplier *= 1.5;
    }

    d.size_multiplier *= tier;
    d.size_multiplier *= (rq >= 2 ? 1.5 : 1.0);

    d.size_multiplier *= (requested_size > 0.0 ? 1.0 : 0.0);

    return d;
}
