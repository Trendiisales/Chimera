#pragma once
#include <string>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace chimera {

struct PositionSnapshot {
    double net_qty = 0.0;
    double avg_price = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    double fees = 0.0;
};

class PositionState {
public:
    PositionState();

    void onFill(const std::string& symbol,
                const std::string& engine_id,
                double price,
                double qty,
                double fee,
                uint64_t event_id);

    PositionSnapshot snapshot(const std::string& symbol) const;
    double totalEquity() const;

private:
    struct Position {
        double net_qty = 0.0;
        double avg_price = 0.0;
        double realized_pnl = 0.0;
        double fees = 0.0;
    };

    std::unordered_map<std::string, Position> m_positions;
    std::atomic<double> m_equity;
};

}
