#pragma once
#include <string>
#include <atomic>
#include "risk/ExchangeTruthReconciler.hpp"

namespace chimera {

class DriftDetector {
public:
    explicit DriftDetector(ExchangeTruthReconciler& rec);

    bool check(const std::string& symbol, double local_qty, double tolerance);
    bool killed() const;

    // External kill trigger — used by market stream on book desync etc.
    // Sets kill state immediately with a logged reason.
    void trigger(const std::string& reason);

    // FIX 4.4: Human-gated kill-clear path.
    // Previously: killed_ was one-way. Once set, system was permanently dead
    // until process restart. No operator could clear without restarting.
    // For a system with a human-gated arm sequence, there must be a matching
    // human-gated kill-clear sequence.
    // Usage: operator confirms drift is understood and positions are reconciled,
    // then calls clear_kill() to allow re-arming.
    void clear_kill();

private:
    ExchangeTruthReconciler& reconciler_;
    std::atomic<bool> killed_{false};
};

}
