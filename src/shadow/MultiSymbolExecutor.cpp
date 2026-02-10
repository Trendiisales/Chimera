#include "shadow/MultiSymbolExecutor.hpp"
#include <chrono>

namespace shadow {

void MultiSymbolExecutor::addSymbol(const SymbolConfig& cfg, ExecMode mode) {
    executors_[cfg.symbol] = std::make_unique<SymbolExecutor>(cfg, mode);
}

void MultiSymbolExecutor::onTick(const std::string& symbol, const Tick& t) {
    auto it = executors_.find(symbol);
    if (it != executors_.end()) {
        it->second->onTick(t);
    }
}

void MultiSymbolExecutor::onSignal(const std::string& symbol, const Signal& s) {
    auto it = executors_.find(symbol);
    if (it != executors_.end()) {
        uint64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        it->second->onSignal(s, ts_ms);
    }
}

double MultiSymbolExecutor::getTotalRealizedPnl() const {
    double total = 0.0;
    for (const auto& pair : executors_) {
        total += pair.second->getRealizedPnL();
    }
    return total;
}

int MultiSymbolExecutor::getTotalActiveLegs() const {
    int total = 0;
    for (const auto& pair : executors_) {
        total += pair.second->getActiveLegs();
    }
    return total;
}

bool MultiSymbolExecutor::isFullyFlat() const {
    return getTotalActiveLegs() == 0;
}

void MultiSymbolExecutor::statusAll() const {
    for (const auto& pair : executors_) {
        pair.second->status();
    }
}

SymbolExecutor* MultiSymbolExecutor::getExecutor(const std::string& symbol) {
    auto it = executors_.find(symbol);
    return (it != executors_.end()) ? it->second.get() : nullptr;
}

const SymbolExecutor* MultiSymbolExecutor::getExecutor(const std::string& symbol) const {
    auto it = executors_.find(symbol);
    return (it != executors_.end()) ? it->second.get() : nullptr;
}

} // namespace shadow
