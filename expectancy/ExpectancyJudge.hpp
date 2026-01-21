#pragma once
#include <unordered_map>
#include <deque>
#include <string>
#include <cmath>
#include <stdexcept>

struct ExpectancyStats {
    double win_rate = 0.0;
    double avg_win = 0.0;
    double avg_loss = 0.0;
    double expectancy = 0.0;
};

struct TradeSample {
    double pnl_bps;
};

class ExpectancyJudge {
public:
    explicit ExpectancyJudge(size_t window = 100)
        : window_(window) {}

    void record(const std::string& engine, double pnl_bps) {
        auto& q = history_[engine];
        q.push_back({pnl_bps});
        if (q.size() > window_)
            q.pop_front();
    }

    ExpectancyStats stats(const std::string& engine) const {
        auto it = history_.find(engine);
        if (it == history_.end() || it->second.empty())
            return {};

        const auto& q = it->second;

        double wins = 0;
        double losses = 0;
        double sum_win = 0;
        double sum_loss = 0;

        for (const auto& t : q) {
            if (t.pnl_bps > 0) {
                wins++;
                sum_win += t.pnl_bps;
            } else {
                losses++;
                sum_loss += std::abs(t.pnl_bps);
            }
        }

        ExpectancyStats s;
        double total = wins + losses;
        if (total == 0)
            return s;

        s.win_rate = wins / total;
        s.avg_win = wins > 0 ? sum_win / wins : 0.0;
        s.avg_loss = losses > 0 ? sum_loss / losses : 0.0;
        s.expectancy = (s.win_rate * s.avg_win) - ((1.0 - s.win_rate) * s.avg_loss);

        return s;
    }

    bool allowed(const std::string& engine) const {
        auto s = stats(engine);
        return s.expectancy >= 0.0;
    }

private:
    size_t window_;
    std::unordered_map<std::string, std::deque<TradeSample>> history_;
};
