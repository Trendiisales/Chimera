#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>

class PnlLedger {
public:
    void record(const std::string& strategy, double delta_nzd);

    double total_nzd() const;

    // NEW: snapshot per-strategy totals
    std::unordered_map<std::string, double> snapshot() const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, double> per_strategy_;
    std::atomic<double> total_{0.0};
};
