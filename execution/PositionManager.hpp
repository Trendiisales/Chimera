#pragma once
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include "../config/V2Config.hpp"
#include "Position.hpp"

namespace ChimeraV2 {

class PositionManager {
public:
    void update_price(const std::string& symbol, double mid) {
        live_price_[symbol] = mid;
    }

    double get_live_price(const std::string& symbol) const {
        auto it = live_price_.find(symbol);
        if (it == live_price_.end()) return 0.0;
        return it->second;
    }

    void add(const Position& pos) {
        positions_.push_back(pos);
    }

    std::vector<Position>& positions() { return positions_; }
    const std::vector<Position>& positions() const { return positions_; }

    int total_open() const {
        return std::count_if(positions_.begin(), positions_.end(),
            [](const Position& p){ return p.open; });
    }

    int symbol_open(const std::string& symbol) const {
        return std::count_if(positions_.begin(), positions_.end(),
            [&](const Position& p){ return p.open && p.symbol == symbol; });
    }

    double floating_pnl() const {
        double total = 0.0;
        for (const auto& p : positions_) {
            if (!p.open) continue;
            total += compute_pnl(p);
        }
        return total;
    }

    bool symbol_recent_stop(const std::string& symbol, uint64_t now_ns) const {
        auto it = last_stop_time_.find(symbol);
        if (it == last_stop_time_.end()) return false;
        return (now_ns - it->second) < (V2Config::REENTRY_GUARD_SECONDS * 1000000000ULL);
    }

    void record_stop(const std::string& symbol, uint64_t now_ns) {
        last_stop_time_[symbol] = now_ns;
    }

    double compute_pnl(const Position& p) const {
        double current = get_live_price(p.symbol);
        if (current == 0.0) return 0.0;

        double diff = (p.side == Side::BUY)
                        ? (current - p.entry_price)
                        : (p.entry_price - current);

        return diff * 1000.0 * p.size;
    }

private:
    std::vector<Position> positions_;
    std::unordered_map<std::string, double> live_price_;
    std::unordered_map<std::string, uint64_t> last_stop_time_;
};

}
