#include "risk/DriftDetector.hpp"
#include <iostream>

using namespace chimera;

DriftDetector::DriftDetector(ExchangeTruthReconciler& rec)
    : reconciler_(rec) {}

bool DriftDetector::check(const std::string& symbol, double local_qty, double tolerance) {
    if (reconciler_.drift_detected(symbol, local_qty, tolerance)) {
        killed_.store(true);
        return true;
    }
    return false;
}

bool DriftDetector::killed() const { return killed_.load(); }

void DriftDetector::trigger(const std::string& reason) {
    killed_.store(true);
    std::cout << "[DRIFT] KILL TRIGGERED: " << reason << "\n";
}

// FIX 4.4: Reset kill state. Operator must confirm drift is understood
// and positions are reconciled before calling this.
// After clear_kill(), the arm sequence can be re-initiated.
void DriftDetector::clear_kill() {
    killed_.store(false);
    std::cout << "[DRIFT] Kill cleared by operator. Re-arm sequence available.\n";
}
