#pragma once
#include "ExecutionSnapshot.hpp"
#include <vector>
#include <mutex>

namespace gui {

class TradeHistory {
public:
    static TradeHistory& instance() {
        static TradeHistory inst;
        return inst;
    }

    void addTrade(const TradeRecord& trade) {
        std::lock_guard<std::mutex> lock(mutex_);
        trades_.push_back(trade);
        if (trades_.size() > 100) { // Keep last 100 trades
            trades_.erase(trades_.begin());
        }
    }

    std::vector<TradeRecord> getTrades() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return trades_;
    }

private:
    TradeHistory() = default;
    mutable std::mutex mutex_;
    std::vector<TradeRecord> trades_;
};

} // namespace gui
