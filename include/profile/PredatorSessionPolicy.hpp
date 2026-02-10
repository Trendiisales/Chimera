// =============================================================================
// PredatorSessionPolicy.hpp - v4.8.0 - PREDATOR SESSION POLICY
// =============================================================================
// PURPOSE: Session-based aggression scaling for Predator profile
//
// Predator should NOT be equally aggressive all day.
// Asia is OFF. No exceptions.
//
// SESSION MATRIX:
// | Session  | Aggression | Risk Mult | Max Trades |
// |----------|------------|-----------|------------|
// | NY_OPEN  | FULL       | 1.0×      | 6          |
// | NY_MID   | REDUCED    | 0.6×      | 3          |
// | LDN      | REDUCED    | 0.5×      | 3          |
// | ASIA     | OFF        | 0×        | 0          |
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <cstdio>
#include <cstdint>

namespace Chimera {

enum class PredatorAggression : uint8_t {
    OFF = 0,
    REDUCED = 1,
    FULL = 2
};

inline const char* predatorAggressionToString(PredatorAggression a) {
    switch (a) {
        case PredatorAggression::OFF:     return "OFF";
        case PredatorAggression::REDUCED: return "REDUCED";
        case PredatorAggression::FULL:    return "FULL";
        default:                          return "UNKNOWN";
    }
}

struct PredatorSessionPolicy {
    PredatorAggression aggression;
    double riskMultiplier;
    int maxTrades;
    
    bool isEnabled() const { return aggression != PredatorAggression::OFF; }
    
    void print() const {
        printf("  Aggression: %-8s | Risk: %.1fx | MaxTrades: %d\n",
               predatorAggressionToString(aggression),
               riskMultiplier,
               maxTrades);
    }
};

// =============================================================================
// SESSION POLICY LOOKUP
// =============================================================================
inline PredatorSessionPolicy getPredatorSessionPolicy(const std::string& session) {
    if (session == "NY_OPEN" || session == "NY")
        return {PredatorAggression::FULL, 1.0, 6};
    if (session == "NY_MID")
        return {PredatorAggression::REDUCED, 0.6, 3};
    if (session == "LDN" || session == "LONDON")
        return {PredatorAggression::REDUCED, 0.5, 3};
    
    // Asia and all other sessions are OFF
    return {PredatorAggression::OFF, 0.0, 0};
}

inline bool isPredatorSessionEnabled(const std::string& session) {
    return getPredatorSessionPolicy(session).isEnabled();
}

inline void printPredatorSessionTable() {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  PREDATOR SESSION POLICY                                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    const char* sessions[] = {"NY_OPEN", "NY_MID", "LDN", "ASIA"};
    for (const auto* sess : sessions) {
        auto policy = getPredatorSessionPolicy(sess);
        printf("║  %-10s: ", sess);
        policy.print();
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

} // namespace Chimera
