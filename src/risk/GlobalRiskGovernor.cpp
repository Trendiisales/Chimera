#include "risk/GlobalRiskGovernor.hpp"
#include <cmath>
#include <iostream>

using namespace chimera;

GlobalRiskGovernor::GlobalRiskGovernor()
    : drift_(reconciler_) {
    max_notional_["BTCUSDT"] = 10000.0;
    max_notional_["ETHUSDT"] = 10000.0;
    max_notional_["SOLUSDT"] = 5000.0;
}

bool GlobalRiskGovernor::pre_check(const std::string& symbol, double price, double qty) {
    std::lock_guard<std::mutex> lock(mtx_);

    // FIX 2.4: Use find() instead of operator[].
    // operator[] inserts a default entry (0.0) if key doesn't exist.
    // Every symbol ever checked would get a zero entry in local_position_
    // and max_notional_, growing both maps unbounded with dead entries.
    //
    // Now: unknown symbols are rejected immediately (no notional limit configured).
    // Known symbols with no position yet are treated as position=0.0 (correct).

    auto max_it = max_notional_.find(symbol);
    if (max_it == max_notional_.end()) {
        // Symbol not in allowed list — block immediately.
        // No map pollution.
        return false;
    }

    double current = 0.0;
    auto pos_it = local_position_.find(symbol);
    if (pos_it != local_position_.end()) {
        current = std::fabs(pos_it->second * price);
    }

    double notional = std::fabs(price * qty);
    return (current + notional) <= max_it->second;
}

void GlobalRiskGovernor::on_execution_ack(const std::string& symbol, double local_qty) {
    std::lock_guard<std::mutex> lock(mtx_);
    local_position_[symbol] += local_qty;
    if (drift_.check(symbol, local_position_[symbol], 0.0001)) {
        std::cout << "[KILL] Position drift detected on " << symbol << "\n";
        killed_.store(true);
    }
}

bool GlobalRiskGovernor::killed() const { return killed_.load(); }

ExchangeTruthReconciler& GlobalRiskGovernor::reconciler() { return reconciler_; }
DriftDetector& GlobalRiskGovernor::drift() { return drift_; }

// B6 FIX: dump acquires mutex — prevents reading torn state during concurrent ack
std::unordered_map<std::string, double> GlobalRiskGovernor::dump_positions() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return local_position_;
}

void GlobalRiskGovernor::clear_positions() {
    std::lock_guard<std::mutex> lock(mtx_);
    local_position_.clear();
}

void GlobalRiskGovernor::restore_position(const std::string& symbol, double qty) {
    std::lock_guard<std::mutex> lock(mtx_);
    local_position_[symbol] = qty;
}

double GlobalRiskGovernor::get_position(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = local_position_.find(symbol);
    return (it != local_position_.end()) ? it->second : 0.0;
}
