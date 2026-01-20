#pragma once
#include <vector>
#include <random>
#include "../telemetry/TelemetryBus.hpp"

class MonteCarloRisk {
public:
    void sample(double pnl) {
        history_.push_back(pnl);
        if (history_.size() > 100) history_.erase(history_.begin());
        publish();
    }

private:
    std::vector<double> history_;

    void publish() {
        if (history_.empty()) return;
        double avg = 0;
        for (double v : history_) avg += v;
        avg /= history_.size();

        TelemetryBus::instance().push("MC", {
            {"band", std::to_string(avg)}
        });
    }
};
