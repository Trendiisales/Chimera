#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

struct DecisionStats {
    uint64_t signals = 0;
    uint64_t orders_sent = 0;
    uint64_t size_scaled = 0;
    uint64_t kill_blocked = 0;

    std::string last_reason;
    double last_btc_stress = 0.0;
    double last_eth_multiplier = 1.0;
};

class DecisionMetrics {
public:
    static DecisionMetrics& instance() {
        static DecisionMetrics inst;
        return inst;
    }

    void recordSignal(const std::string& sym) {
        std::lock_guard<std::mutex> g(mtx_);
        data_[sym].signals++;
    }

    void recordOrder(const std::string& sym, const std::string& reason) {
        std::lock_guard<std::mutex> g(mtx_);
        auto& d = data_[sym];
        d.orders_sent++;
        d.last_reason = reason;
    }

    void recordScaled(const std::string& sym, double mult, double stress) {
        std::lock_guard<std::mutex> g(mtx_);
        auto& d = data_[sym];
        d.size_scaled++;
        d.last_eth_multiplier = mult;
        d.last_btc_stress = stress;
        d.last_reason = "SIZE_SCALED";
    }

    void recordKill(const std::string& sym) {
        std::lock_guard<std::mutex> g(mtx_);
        auto& d = data_[sym];
        d.kill_blocked++;
        d.last_reason = "KILL_BLOCK";
    }

    std::unordered_map<std::string, DecisionStats> snapshot() {
        std::lock_guard<std::mutex> g(mtx_);
        return data_;
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, DecisionStats> data_;
};
