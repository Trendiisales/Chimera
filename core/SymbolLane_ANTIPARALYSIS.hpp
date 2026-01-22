#ifndef SYMBOLLANE_ANTIPARALYSIS_HPP
#define SYMBOLLANE_ANTIPARALYSIS_HPP

#include <string>
#include <cstdint>
#include "include/chimera/execution/ExchangeIO.hpp"
#include "../telemetry/TelemetryBus.hpp"

// A lane without a stored hash is an incomplete lane.
class SymbolLane {
public:
    // Hash MUST be provided at construction - never recomputed
    explicit SymbolLane(std::string sym, uint32_t hash);
    
    void tick();
    void onTick(const chimera::MarketTick& tick);
    
    // Getters for routing
    uint32_t symbolHash() const { return symbol_hash_; }
    const std::string& symbolName() const { return symbol_; }

private:
    // Identity
    std::string symbol_;
    uint32_t symbol_hash_;  // Stored, never recomputed
    
    // Trading state
    double net_bps_;
    double dd_bps_;
    int trade_count_;
    double fees_;
    double alloc_;
    double leverage_;
    double last_price_;
    double position_;
    double last_mid_;
    int ticks_since_trade_;
    int warmup_ticks_;
};

#endif // SYMBOLLANE_ANTIPARALYSIS_HPP
