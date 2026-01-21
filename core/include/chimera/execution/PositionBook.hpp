#pragma once

#include <string>
#include <unordered_map>

namespace chimera {

struct Position {
    double net_qty = 0.0;
    double avg_price = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
};

class PositionBook {
public:
    void onFill(
        const std::string& symbol,
        bool is_buy,
        double qty,
        double price
    );

    void markToMarket(const std::string& symbol, double last_price);

    const Position& get(const std::string& symbol) const;
    double totalExposure() const;

    // For iteration (EmergencyFlatten, StatePersistence)
    const std::unordered_map<std::string, Position>& all() const;
    
    // For loading from persistence
    void restore(const std::string& symbol, const Position& pos);

private:
    std::unordered_map<std::string, Position> positions;
};

}
