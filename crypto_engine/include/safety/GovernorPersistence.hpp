#pragma once
// =============================================================================
// GovernorPersistence.hpp - State Persistence Across Restarts
// =============================================================================
// v4.9.8: Prevents aggressive re-entry after restart into bad regime
//
// WHY THIS MATTERS:
//   Right now:
//     Market degrades → governors tighten
//     You restart → everything resets
//     Engine re-enters bad regime at full aggression
//     Same losses repeat
//
//   With persistence:
//     Market degrades → governors tighten → state saved
//     You restart → state restored
//     Engine enters with appropriate caution
//     Recovery happens gradually
//
// WHAT IS PERSISTED (minimal, sufficient):
//   - Governor state (NORMAL, CLAMPED_FEE, etc.)
//   - Relax steps
//   - Adjusted parameters
//
// WHAT IS NOT PERSISTED:
//   - Rolling metrics (recomputed)
//   - PnL (tracked elsewhere)
//   - Health stats (recomputed)
// =============================================================================

#include <fstream>
#include <string>
#include <cstdio>
#include <sys/stat.h>

#include "CryptoSafetyGovernor.hpp"

namespace Chimera {
namespace Crypto {

// =============================================================================
// Persisted State Structure
// =============================================================================
struct PersistedGovernorState {
    GovernorState state;
    int relax_steps;
    double entry_confidence_min;
    double expectancy_min_bps;
    int confirmation_ticks;
    int maker_timeout_ms;
    double governor_heat;
};

// =============================================================================
// Simple JSON-like persistence (no external dependencies)
// =============================================================================

inline void ensureStateDir() {
    mkdir("state", 0755);
}

inline bool saveGovernorState(const char* symbol, const PersistedGovernorState& s) {
    ensureStateDir();
    
    char filename[256];
    snprintf(filename, sizeof(filename), "state/governor_%s.txt", symbol);
    
    FILE* f = fopen(filename, "w");
    if (!f) {
        printf("[GOVERNOR] Failed to save state for %s\n", symbol);
        return false;
    }
    
    fprintf(f, "state=%d\n", static_cast<int>(s.state));
    fprintf(f, "relax_steps=%d\n", s.relax_steps);
    fprintf(f, "entry_confidence_min=%.6f\n", s.entry_confidence_min);
    fprintf(f, "expectancy_min_bps=%.6f\n", s.expectancy_min_bps);
    fprintf(f, "confirmation_ticks=%d\n", s.confirmation_ticks);
    fprintf(f, "maker_timeout_ms=%d\n", s.maker_timeout_ms);
    fprintf(f, "governor_heat=%.6f\n", s.governor_heat);
    
    fclose(f);
    printf("[GOVERNOR] Saved state for %s: gov=%s heat=%.2f\n", 
           symbol, governorStateStr(s.state), s.governor_heat);
    return true;
}

inline bool loadGovernorState(const char* symbol, PersistedGovernorState& s) {
    char filename[256];
    snprintf(filename, sizeof(filename), "state/governor_%s.txt", symbol);
    
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("[GOVERNOR] No saved state for %s, starting fresh\n", symbol);
        return false;
    }
    
    int state_int = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "state=%d", &state_int) == 1) {
            s.state = static_cast<GovernorState>(state_int);
        } else if (sscanf(line, "relax_steps=%d", &s.relax_steps) == 1) {
            // parsed
        } else if (sscanf(line, "entry_confidence_min=%lf", &s.entry_confidence_min) == 1) {
            // parsed
        } else if (sscanf(line, "expectancy_min_bps=%lf", &s.expectancy_min_bps) == 1) {
            // parsed
        } else if (sscanf(line, "confirmation_ticks=%d", &s.confirmation_ticks) == 1) {
            // parsed
        } else if (sscanf(line, "maker_timeout_ms=%d", &s.maker_timeout_ms) == 1) {
            // parsed
        } else if (sscanf(line, "governor_heat=%lf", &s.governor_heat) == 1) {
            // parsed
        }
    }
    
    fclose(f);
    printf("[GOVERNOR] Loaded state for %s: gov=%s heat=%.2f conf=%.2f exp=%.2f\n", 
           symbol, governorStateStr(s.state), s.governor_heat,
           s.entry_confidence_min, s.expectancy_min_bps);
    return true;
}

// =============================================================================
// Helper to create state from engine state
// =============================================================================
inline PersistedGovernorState captureState(const SymbolParams& params,
                                            GovernorState state,
                                            int relax_steps,
                                            double heat) {
    return PersistedGovernorState{
        state,
        relax_steps,
        params.entry_confidence_min,
        params.expectancy_min_bps,
        params.confirmation_ticks,
        params.maker_timeout_ms,
        heat
    };
}

// =============================================================================
// Apply restored state to params
// =============================================================================
inline void applyRestoredState(SymbolParams& params, 
                                const PersistedGovernorState& s,
                                const HardBounds& bounds) {
    // Apply with bounds checking
    params.entry_confidence_min = std::clamp(s.entry_confidence_min,
                                              bounds.min_confidence,
                                              bounds.max_confidence);
    params.expectancy_min_bps = std::clamp(s.expectancy_min_bps,
                                            bounds.min_expectancy_bps,
                                            bounds.max_expectancy_bps);
    params.confirmation_ticks = std::clamp(s.confirmation_ticks,
                                            bounds.min_confirmation_ticks,
                                            bounds.max_confirmation_ticks);
    params.maker_timeout_ms = std::clamp(s.maker_timeout_ms,
                                          bounds.min_maker_timeout_ms,
                                          bounds.max_maker_timeout_ms);
}

} // namespace Crypto
} // namespace Chimera
