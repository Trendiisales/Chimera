// =============================================================================
// PredatorSymbolConfig.hpp - v4.8.0 - PREDATOR SYMBOL CONFIGURATION
// =============================================================================
// PURPOSE: Symbol-specific thresholds for Predator profile
//
// HARD RULE: If symbol parameters are missing → Predator does not trade.
//            No defaults. No assumptions.
//
// Speed edge is symbol dependent. Using one size fits all kills expectancy.
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <cstdint>
#include <cstdio>

namespace Chimera {

struct PredatorSymbolConfig {
    double minImbalance;        // Minimum order book imbalance to consider
    uint64_t maxAcceptMs;       // Max milliseconds for acceptance confirmation
    double edgeDecayExit;       // Edge decay threshold for exit (e.g., 0.60 = 60%)
    uint64_t maxHoldNs;         // Maximum hold time in nanoseconds
    bool enabled;               // Whether this symbol is tradeable
    
    // Convenience getters
    uint64_t maxAcceptNs() const { return maxAcceptMs * 1'000'000ULL; }
    
    void print() const {
        printf("  Imbalance: %.2f | Accept: %lums | EdgeExit: %.0f%% | MaxHold: %.1fs | %s\n",
               minImbalance,
               static_cast<unsigned long>(maxAcceptMs),
               edgeDecayExit * 100.0,
               static_cast<double>(maxHoldNs) / 1e9,
               enabled ? "ENABLED" : "DISABLED");
    }
};

// =============================================================================
// PREDATOR SYMBOL TABLE (v1 – SAFE, AGGRESSIVE, REALISTIC)
// =============================================================================
// | Symbol  | Imbalance ≥ | Accept ms | Edge Decay Exit | Max Hold | Notes              |
// |---------|-------------|-----------|-----------------|----------|--------------------| 
// | NAS100  | 0.75        | ≤120ms    | 60%             | 1.8s     | Best speed edge    |
// | US30    | 0.70        | ≤150ms    | 55%             | 2.0s     | Noisier, looser    |
// | SPX500  | 0.80        | ≤100ms    | 65%             | 1.5s     | Clean but selective|
// | XAUUSD  | 0.72        | ≤180ms    | 50%             | 2.5s     | Needs more room    |
// =============================================================================

inline PredatorSymbolConfig getPredatorConfig(const std::string& sym) {
    if (sym == "NAS100")
        return {0.75, 120, 0.60, 1'800'000'000ULL, true};
    if (sym == "US30")
        return {0.70, 150, 0.55, 2'000'000'000ULL, true};
    if (sym == "SPX500")
        return {0.80, 100, 0.65, 1'500'000'000ULL, true};
    if (sym == "XAUUSD")
        return {0.72, 180, 0.50, 2'500'000'000ULL, true};

    // Unknown symbol - DISABLED (no defaults, no assumptions)
    return {0.0, 0, 1.0, 0, false};
}

inline bool isPredatorSymbolEnabled(const std::string& sym) {
    return getPredatorConfig(sym).enabled;
}

inline void printPredatorSymbolTable() {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  PREDATOR SYMBOL CONFIGURATION                                ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    const char* symbols[] = {"NAS100", "US30", "SPX500", "XAUUSD"};
    for (const auto* sym : symbols) {
        auto cfg = getPredatorConfig(sym);
        printf("║  %-8s: ", sym);
        cfg.print();
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

} // namespace Chimera
