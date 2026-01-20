#pragma once
#include <unordered_map>
#include <string>
#include "../telemetry/TelemetryBus.hpp"

struct Bucket {
    double pnl = 0;
    double expectancy = 0;
};

class CapitalRotationAI {
public:
    void update(const std::string& sym, double pnl) {
        auto& b = buckets_[sym];
        b.pnl += pnl;
        b.expectancy = b.pnl / (++ticks_);

        rebalance();
    }

    double allocation(const std::string& sym) {
        if (total_ == 0) return 0;
        return buckets_[sym].expectancy / total_;
    }

private:
    std::unordered_map<std::string, Bucket> buckets_;
    double total_ = 0;
    int ticks_ = 0;

    void rebalance() {
        total_ = 0;
        for (auto& [k, b] : buckets_) total_ += b.expectancy;

        for (auto& [k, b] : buckets_) {
            TelemetryBus::instance().push("DIVERGE", {
                {"symbol", k},
                {"shadow", std::to_string(b.expectancy)},
                {"live", std::to_string(b.pnl)},
                {"gap", std::to_string(b.pnl - b.expectancy)}
            });
        }
    }
};
