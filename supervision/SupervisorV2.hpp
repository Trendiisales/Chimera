#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include "SymbolController.hpp"
#include "../risk/CapitalGovernor.hpp"
#include "../execution/ExecutionAuthority.hpp"

namespace ChimeraV2 {

/**
 * Supervisor - Manages per-symbol engine stacks
 * 
 * Key Design:
 * - Each symbol has its own SymbolController
 * - Capital is isolated per symbol
 * - Execution decisions are per symbol
 * - NO cross-symbol contamination
 */
class Supervisor {
public:
    Supervisor() = default;

    // Get or create controller for a symbol
    SymbolController& get_controller(const std::string& symbol) {
        auto it = controllers_.find(symbol);
        if (it == controllers_.end()) {
            controllers_[symbol] = std::make_unique<SymbolController>(symbol);
        }
        return *controllers_[symbol];
    }

    // Process tick for specific symbol only
    void on_market_tick(const std::string& symbol, double bid, double ask, uint64_t timestamp_ns) {
        auto it = controllers_.find(symbol);
        if (it != controllers_.end()) {
            it->second->on_market_tick(bid, ask, timestamp_ns);
        }
    }

    // Get all symbols being managed
    std::vector<std::string> symbols() const {
        std::vector<std::string> result;
        for (const auto& [symbol, controller] : controllers_) {
            result.push_back(symbol);
        }
        return result;
    }

    // Check if symbol is registered
    bool has_symbol(const std::string& symbol) const {
        return controllers_.find(symbol) != controllers_.end();
    }

    // Get controller (const version)
    const SymbolController* get_controller_const(const std::string& symbol) const {
        auto it = controllers_.find(symbol);
        return it != controllers_.end() ? it->second.get() : nullptr;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<SymbolController>> controllers_;
};

} // namespace ChimeraV2
