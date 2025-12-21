#include "accounting/PnlLedger.hpp"

void PnlLedger::record(const std::string& strategy, double delta_nzd) {
    {
        std::lock_guard<std::mutex> g(mtx_);
        per_strategy_[strategy] += delta_nzd;
    }
    total_.fetch_add(delta_nzd, std::memory_order_relaxed);
}

double PnlLedger::total_nzd() const {
    return total_.load(std::memory_order_relaxed);
}

std::unordered_map<std::string, double> PnlLedger::snapshot() const {
    std::lock_guard<std::mutex> g(mtx_);
    return per_strategy_;
}
