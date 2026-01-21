#pragma once

#include <string>
#include <atomic>
#include <cstdint>

struct ControlDecision {
    bool    allow;
    double  size_multiplier;
    uint32_t flags;
};

class ControlPlane {
public:
    enum Flag : uint32_t {
        NONE        = 0,
        COST_FAIL  = 1 << 0,
        SESSION    = 1 << 1,
        REGIME     = 1 << 2,
        CAPITAL    = 1 << 3,
        LATENCY    = 1 << 4,
        KILL       = 1 << 5
    };

    ControlPlane();

    void setKill(bool v);
    void setSessionAllowed(bool v);
    void setRegimeQuality(int q);
    void setCapitalTier(int t);
    void setLatencyRank(int r);

    ControlDecision decide(const std::string& engine,
                           double edge_bps,
                           double cost_bps,
                           double requested_size) const;

private:
    std::atomic<bool> kill_;
    std::atomic<bool> session_allowed_;
    std::atomic<int>  regime_quality_;
    std::atomic<int>  capital_tier_;
    std::atomic<int>  latency_rank_;
};
