#pragma once
#include "SymbolExecutor.hpp"
#include "execution/ExecutionRouter.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace shadow {

class MultiSymbolExecutor {
public:
    MultiSymbolExecutor();
    
    // Symbol registration
    void addSymbol(const SymbolConfig& cfg, ExecMode mode);
    
    // Market data routing
    void onTick(const std::string& symbol, const Tick& t);
    
    // Signal routing
    void onSignal(const std::string& symbol, const Signal& s);
    
    // Portfolio queries
    double getTotalRealizedPnl() const;
    int getTotalActiveLegs() const;
    bool isFullyFlat() const;
    
    // Status/monitoring
    void statusAll() const;
    
    // Symbol-specific access
    SymbolExecutor* getExecutor(const std::string& symbol);
    const SymbolExecutor* getExecutor(const std::string& symbol) const;
    
    // ExecutionRouter access
    ExecutionRouter& router() { return router_; }

private:
    ExecutionRouter router_;
    std::unordered_map<std::string, std::unique_ptr<SymbolExecutor>> executors_;
};

} // namespace shadow
