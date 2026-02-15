#pragma once
#include <vector>
#include <memory>
#include <string>
#include "../engines/IEngine.hpp"
#include "../execution/Position.hpp"

namespace ChimeraV2 {

/**
 * SymbolController - Per-symbol engine stack with strict isolation
 * 
 * Each symbol (XAUUSD, XAGUSD) gets its own controller.
 * Engines registered to a controller ONLY see ticks for that symbol.
 * Capital, positions, and PnL are completely isolated.
 */
class SymbolController {
public:
    explicit SymbolController(const std::string& symbol)
        : symbol_(symbol)
    {}

    // Register an engine for this symbol only
    void register_engine(IEngine* engine) {
        engines_.push_back(engine);
    }

    // Process market tick - only engines for THIS symbol see it
    void on_market_tick(double bid, double ask, uint64_t timestamp_ns) {
        for (auto* engine : engines_) {
            engine->on_market(symbol_, bid, ask, timestamp_ns);
        }
    }

    // Get all engines for this symbol
    const std::vector<IEngine*>& engines() const {
        return engines_;
    }

    const std::string& symbol() const {
        return symbol_;
    }

    size_t engine_count() const {
        return engines_.size();
    }

private:
    std::string symbol_;
    std::vector<IEngine*> engines_;
};

} // namespace ChimeraV2
