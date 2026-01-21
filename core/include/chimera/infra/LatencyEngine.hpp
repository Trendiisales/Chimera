#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

namespace chimera {

struct LatencyStats {
    double rest_rtt_ms = 0.0;
    double ws_lag_ms = 0.0;
};

class LatencyEngine {
public:
    void onRestSend(const std::string& venue, uint64_t id);
    void onRestAck(const std::string& venue, uint64_t id);

    void onWsTick(
        const std::string& venue,
        uint64_t exchange_ts_ms
    );

    LatencyStats stats(
        const std::string& venue
    ) const;

private:
    uint64_t nowMs() const;

private:
    struct Pending {
        uint64_t ts;
    };

    mutable std::mutex mtx;
    std::unordered_map<std::string,
        std::unordered_map<uint64_t, Pending>> pending;
    std::unordered_map<std::string, LatencyStats> data;
};

}
