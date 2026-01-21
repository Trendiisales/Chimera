#pragma once
#include <unordered_map>
#include <string>
#include <cmath>

struct DrawdownStats {
    double peak_bps = 0.0;
    double trough_bps = 0.0;
    double drawdown_bps = 0.0;
};

class DrawdownSentinel {
public:
    explicit DrawdownSentinel(double max_dd_bps = 20.0)
        : max_dd_bps_(max_dd_bps) {}

    void update(const std::string& engine, double pnl_bps) {
        auto& s = stats_[engine];

        if (!initialized_[engine]) {
            s.peak_bps = pnl_bps;
            s.trough_bps = pnl_bps;
            initialized_[engine] = true;
            return;
        }

        if (pnl_bps > s.peak_bps)
            s.peak_bps = pnl_bps;

        if (pnl_bps < s.trough_bps)
            s.trough_bps = pnl_bps;

        s.drawdown_bps = s.peak_bps - s.trough_bps;
    }

    bool allowed(const std::string& engine) const {
        auto it = stats_.find(engine);
        if (it == stats_.end())
            return true;
        return it->second.drawdown_bps <= max_dd_bps_;
    }

    DrawdownStats stats(const std::string& engine) const {
        auto it = stats_.find(engine);
        if (it == stats_.end())
            return {};
        return it->second;
    }

private:
    double max_dd_bps_;
    std::unordered_map<std::string, DrawdownStats> stats_;
    std::unordered_map<std::string, bool> initialized_;
};
