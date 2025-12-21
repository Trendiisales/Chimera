#pragma once

#include <unordered_map>
#include <string>

struct Position {
    double qty = 0.0;
    double avg_price = 0.0;
};

class PositionTracker {
public:
    void on_fill(const std::string& symbol,
                 const std::string& side,
                 double price,
                 double qty);

    double realized_pnl() const;
    double unrealized_pnl(const std::string& symbol, double mid) const;
    double total_unrealized(const std::unordered_map<std::string,double>& mids) const;

private:
    std::unordered_map<std::string, Position> positions_;
    double realized_{0.0};
};
