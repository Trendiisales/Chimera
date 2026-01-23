#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace chimera::state {

struct PositionState {
    double qty;
    double avg_price;
};

struct LaneState {
    double ofi;
    double venue_bias;
    double capital_weight;
};

struct Snapshot {
    uint64_t ts_ns;
    std::unordered_map<std::string, PositionState> positions;
    std::unordered_map<std::string, LaneState> lanes;
};

void saveSnapshot(const Snapshot& snap, const std::string& path);
Snapshot loadSnapshot(const std::string& path);

}
