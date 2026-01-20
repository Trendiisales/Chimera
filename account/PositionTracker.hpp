#pragma once
#include <unordered_map>
#include <string>
#include <mutex>

struct Position {
    double qty = 0.0;
    double avg_price = 0.0;
    double realized_pnl = 0.0;
};

class PositionTracker {
    std::mutex mtx;
    std::unordered_map<std::string, Position> positions;

public:
    void onFill(const std::string& sym, double qty, double price);
    double unrealized(const std::string& sym, double mark);
    Position get(const std::string& sym);
};
