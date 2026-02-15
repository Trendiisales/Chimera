#pragma once
#include "SymbolState.hpp"
#include <deque>
#include <unordered_map>
#include <cmath>
#include <numeric>

namespace ChimeraV2 {

class MarketStateBuilder {
public:
    void update(SymbolState& s, double bid, double ask, uint64_t ts_ns) {
        double mid = (bid + ask) * 0.5;
        double spread = ask - bid;

        if (!prices_[s.symbol].empty()) {
            double prev = prices_[s.symbol].back();
            s.velocity = mid - prev;
        }

        prices_[s.symbol].push_back(mid);
        if (prices_[s.symbol].size() > 100)
            prices_[s.symbol].pop_front();

        s.mid = mid;
        s.spread = spread;
        s.timestamp_ns = ts_ns;

        compute_volatility(s);
        compute_structural_momentum(s);
        compute_compression(s);
        compute_acceleration(s);
    }

private:
    std::unordered_map<std::string, std::deque<double>> prices_;

    void compute_volatility(SymbolState& s) {
        auto& dq = prices_[s.symbol];
        if (dq.size() < 5) return;

        double mean = std::accumulate(dq.begin(), dq.end(), 0.0) / dq.size();

        double var = 0.0;
        for (auto v : dq) {
            var += (v - mean) * (v - mean);
        }
        var /= dq.size();

        s.short_vol = std::sqrt(var);
        s.long_vol = s.short_vol * 1.5;
    }

    void compute_structural_momentum(SymbolState& s) {
        auto& dq = prices_[s.symbol];
        if (dq.size() < 10) return;

        double sum = 0.0;
        for (size_t i = dq.size() - 9; i < dq.size(); ++i) {
            sum += dq[i] - dq[i - 1];
        }

        s.structural_momentum = sum;
    }

    void compute_compression(SymbolState& s) {
        if (s.long_vol > 0.0)
            s.compression_ratio = s.short_vol / s.long_vol;
    }

    void compute_acceleration(SymbolState& s) {
        s.acceleration = s.velocity * 10.0;
    }
};

}
