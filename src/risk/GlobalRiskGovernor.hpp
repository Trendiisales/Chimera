#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>

#include "risk/ExchangeTruthReconciler.hpp"
#include "risk/DriftDetector.hpp"

namespace chimera {

class GlobalRiskGovernor {
public:
    GlobalRiskGovernor();

    bool pre_check(const std::string& symbol, double price, double qty);
    void on_execution_ack(const std::string& symbol, double local_qty);
    bool killed() const;
    ExchangeTruthReconciler& reconciler();
    DriftDetector& drift();

    // Snapshot support — used by ContextSnapshotter
    // B6 FIX: all dump methods acquire mutex before copying
    std::unordered_map<std::string, double> dump_positions() const;
    void clear_positions();
    void restore_position(const std::string& symbol, double qty);

    // ---------------------------------------------------------------------------
    // Single-symbol position read — for StrategyRunner tick injection.
    // Returns 0.0 if symbol has no position yet. Acquires mutex.
    // ---------------------------------------------------------------------------
    double get_position(const std::string& symbol) const;

private:
    std::unordered_map<std::string, double> max_notional_;
    std::unordered_map<std::string, double> local_position_;
    mutable std::mutex mtx_;

    ExchangeTruthReconciler reconciler_;
    DriftDetector drift_;
    std::atomic<bool> killed_{false};
};

}
