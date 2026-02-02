#pragma once
#include <unordered_map>
#include <string>
#include <mutex>

namespace chimera {

struct ExchangePosition {
    std::string symbol;
    double qty;
    double entry_price;
};

class ExchangeTruthReconciler {
public:
    void on_exchange_position(const ExchangePosition& pos);
    bool get_position(const std::string& symbol, ExchangePosition& out);
    bool drift_detected(const std::string& symbol, double local_qty, double tolerance);

private:
    std::unordered_map<std::string, ExchangePosition> positions_;
    std::mutex mtx_;
};

}
