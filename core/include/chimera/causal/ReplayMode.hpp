#pragma once

#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace chimera {

// Deterministic replay mode freezes all adaptive components
// to ensure identical replays of historical data
class ReplayMode {
public:
    // Enable/disable deterministic mode
    void enable();
    void disable();
    bool isEnabled() const;

    // Frozen clock (no real-time drift)
    void setFrozenTimestamp(int64_t ts_ms);
    int64_t frozenTimestamp() const;

    // Frozen latency (use recorded values, not measured)
    void setFrozenLatency(const std::string& venue, double rtt_ms, double ws_lag_ms);
    bool hasFrozenLatency(const std::string& venue) const;
    double frozenRTT(const std::string& venue) const;
    double frozenWSLag(const std::string& venue) const;

    // Frozen routing (disable venue learning)
    void setFrozenVenue(const std::string& symbol, const std::string& venue);
    bool hasFrozenVenue(const std::string& symbol) const;
    std::string frozenVenue(const std::string& symbol) const;

    // Frozen capital (disable dynamic allocation)
    void setFrozenCapital(const std::string& engine, double weight);
    bool hasFrozenCapital(const std::string& engine) const;
    double frozenCapital(const std::string& engine) const;

private:
    std::atomic<bool> enabled_{false};
    std::atomic<int64_t> frozen_timestamp_{0};

    struct LatencyState {
        double rtt_ms;
        double ws_lag_ms;
    };

    std::unordered_map<std::string, LatencyState> frozen_latency_;
    std::unordered_map<std::string, std::string> frozen_venues_;
    std::unordered_map<std::string, double> frozen_capital_;
};

}
