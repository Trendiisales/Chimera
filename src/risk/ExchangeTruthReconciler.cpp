#include "risk/ExchangeTruthReconciler.hpp"
#include <cmath>

using namespace chimera;

void ExchangeTruthReconciler::on_exchange_position(const ExchangePosition& pos) {
    std::lock_guard<std::mutex> lock(mtx_);
    positions_[pos.symbol] = pos;
}

bool ExchangeTruthReconciler::get_position(const std::string& symbol, ExchangePosition& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return false;
    out = it->second;
    return true;
}

bool ExchangeTruthReconciler::drift_detected(const std::string& symbol,
                                              double local_qty, double tolerance) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return false;
    return std::fabs(it->second.qty - local_qty) > tolerance;
}
