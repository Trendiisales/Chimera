#pragma once
#include <unordered_map>
#include <string>
#include <chrono>
#include "../telemetry/TelemetryBus.hpp"

struct KillStats {
    double avg_edge = 0;
    double avg_latency = 0;
    int samples = 0;
};

class AutoKillLearner {
public:
    void observe(const std::string& sym, double edge, double latency_ms) {
        auto& s = stats_[sym];
        s.avg_edge = (s.avg_edge * s.samples + edge) / (s.samples + 1);
        s.avg_latency = (s.avg_latency * s.samples + latency_ms) / (s.samples + 1);
        s.samples++;

        TelemetryBus::instance().push("EDGE", {
            {"engine", sym},
            {"edge", std::to_string(s.avg_edge)},
            {"trend", s.avg_edge < 0.1 ? "DECAY" : "HEALTHY"}
        });
    }

    bool shouldKill(const std::string& sym) {
        auto& s = stats_[sym];
        return s.avg_edge < 0.05 || s.avg_latency > 50.0;
    }

private:
    std::unordered_map<std::string, KillStats> stats_;
};
