#pragma once
#include <unordered_map>
#include <string>

namespace chimera::fitness {

struct VenueStats {
    double avg_latency = 0;
    double avg_slippage = 0;
    double reject_rate = 0;
    double fee_bps = 0;
};

class VenueFitness {
public:
    void update(const std::string& venue, double latency, double slippage, bool reject, double fee) {
        auto& v = stats[venue];
        v.avg_latency = (v.avg_latency * 0.9) + (latency * 0.1);
        v.avg_slippage = (v.avg_slippage * 0.9) + (slippage * 0.1);
        v.reject_rate = (v.reject_rate * 0.9) + ((reject ? 1.0 : 0.0) * 0.1);
        v.fee_bps = fee;
    }

    double score(const std::string& venue) const {
        auto it = stats.find(venue);
        if (it == stats.end()) return 0;

        const auto& v = it->second;
        return 1.0 / (1.0 + v.avg_latency + v.avg_slippage + v.reject_rate + v.fee_bps);
    }

private:
    std::unordered_map<std::string, VenueStats> stats;
};

}
