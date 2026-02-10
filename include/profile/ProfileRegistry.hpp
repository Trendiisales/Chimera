// =============================================================================
// ProfileRegistry.hpp - v4.9.0 - MASTER PROFILE REGISTRY
// =============================================================================
// PURPOSE: Central include for all Chimera profit engines
//
// ENGINES IN THIS RELEASE:
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │ ENGINE              │ BEHAVIOR           │ FREQUENCY │ RISK  │ EDGE    │
// ├─────────────────────────────────────────────────────────────────────────┤
// │ PREDATOR            │ Microstructure     │ High      │ 0.05% │ Speed   │
// │ OPEN_RANGE          │ Time-based liq     │ Low       │ 0.15% │ Session │
// │ STOP_RUN_FADE       │ Liquidity failure  │ Medium    │ 0.05% │ Fade    │
// │ SESSION_HANDOFF     │ Structural rebal   │ Very Low  │ 0.20% │ Flow    │
// │ VWAP_DEFENSE        │ Inventory defense  │ Moderate  │ 0.07% │ VWAP    │
// │ LIQUIDITY_VACUUM    │ Mechanical gaps    │ Low-Mod   │ 0.05% │ Gap     │
// └─────────────────────────────────────────────────────────────────────────┘
//
// KEY DESIGN PRINCIPLES:
//   ✓ Failure modes are UNCORRELATED
//   ✓ Each engine has a DISTINCT edge source
//   ✓ All engines respect Chimera governance hierarchy
//   ✓ No engine can override global risk controls
//
// GOVERNANCE HIERARCHY (IMMUTABLE):
//   1. Latency / Shock / Risk exits    ← HIGHEST
//   2. DailyHealthAudit (hard stop)
//   3. RollingEdgeAudit (slow throttle)
//   4. EdgeRecoveryRules (conservative re-enable)
//   5. GoNoGoGate (session decision)
//   6. Engine / Profile logic          ← LOWEST
//
// DEPLOYMENT ORDER (RECOMMENDED):
//   1. PREDATOR → already deployed
//   2. OPEN_RANGE → shadow first
//   3. VWAP_DEFENSE → shadow first
//   4. STOP_RUN_FADE → shadow first
//   5. LIQUIDITY_VACUUM → shadow first
//   6. SESSION_HANDOFF → live (already low risk)
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-01
// =============================================================================
#pragma once

// =============================================================================
// EXISTING PROFILE
// =============================================================================
#include "profile/PredatorProfile.hpp"
#include "profile/PredatorSymbolConfig.hpp"
#include "profile/PredatorSessionPolicy.hpp"
#include "profile/PredatorIdleReason.hpp"

// =============================================================================
// NEW PROFIT ENGINES v4.9.0
// =============================================================================
#include "profile/OpenRangeProfile.hpp"
#include "profile/StopRunFadeProfile.hpp"
#include "profile/SessionHandoffProfile.hpp"
#include "profile/VwapDefenseProfile.hpp"
#include "profile/LiquidityVacuumProfile.hpp"

// =============================================================================
// SHARED DEPENDENCIES
// =============================================================================
#include "micro/VwapAcceleration.hpp"
#include "risk/LossVelocity.hpp"
#include "shared/SessionDetector.hpp"
#include "audit/GoNoGoGate.hpp"

namespace Chimera {

// =============================================================================
// PROFILE TYPE ENUM
// =============================================================================
enum class ProfileType : uint8_t {
    PREDATOR = 0,
    OPEN_RANGE = 1,
    STOP_RUN_FADE = 2,
    SESSION_HANDOFF = 3,
    VWAP_DEFENSE = 4,
    LIQUIDITY_VACUUM = 5,
    
    COUNT = 6
};

inline const char* profileTypeToString(ProfileType t) {
    switch (t) {
        case ProfileType::PREDATOR:         return "PREDATOR";
        case ProfileType::OPEN_RANGE:       return "OPEN_RANGE";
        case ProfileType::STOP_RUN_FADE:    return "STOP_RUN_FADE";
        case ProfileType::SESSION_HANDOFF:  return "SESSION_HANDOFF";
        case ProfileType::VWAP_DEFENSE:     return "VWAP_DEFENSE";
        case ProfileType::LIQUIDITY_VACUUM: return "LIQUIDITY_VACUUM";
        default:                            return "UNKNOWN";
    }
}

// =============================================================================
// PROFILE CORRELATION MATRIX (for reference)
// =============================================================================
// Failure modes are designed to be uncorrelated:
//
//           PRED  ORE   SRF   SH    VD    LV
// PRED       1    0.1   0.2   0.05  0.15  0.1
// ORE       0.1    1    0.1   0.2   0.15  0.05
// SRF       0.2   0.1    1    0.1   0.2   0.3
// SH        0.05  0.2   0.1    1    0.1   0.05
// VD        0.15  0.15  0.2   0.1    1    0.15
// LV        0.1   0.05  0.3   0.05  0.15   1
//
// This means: when PREDATOR fails, VWAP_DEFENSE may still profit
// =============================================================================

// =============================================================================
// PROFILE MANAGER (for multi-profile orchestration)
// =============================================================================
class ProfileManager {
public:
    ProfileManager() = default;
    
    // Get profile instances
    PredatorProfile& predator() { return predator_; }
    OpenRangeProfile& openRange() { return openRange_; }
    StopRunFadeProfile& stopRunFade() { return stopRunFade_; }
    SessionHandoffProfile& sessionHandoff() { return sessionHandoff_; }
    VwapDefenseProfile& vwapDefense() { return vwapDefense_; }
    LiquidityVacuumProfile& liquidityVacuum() { return liquidityVacuum_; }
    
    // Const versions
    const PredatorProfile& predator() const { return predator_; }
    const OpenRangeProfile& openRange() const { return openRange_; }
    const StopRunFadeProfile& stopRunFade() const { return stopRunFade_; }
    const SessionHandoffProfile& sessionHandoff() const { return sessionHandoff_; }
    const VwapDefenseProfile& vwapDefense() const { return vwapDefense_; }
    const LiquidityVacuumProfile& liquidityVacuum() const { return liquidityVacuum_; }
    
    // Enable/Disable all profiles
    void enableAll() {
        predator_.enable();
        openRange_.enable();
        stopRunFade_.enable();
        sessionHandoff_.enable();
        vwapDefense_.enable();
        liquidityVacuum_.enable();
    }
    
    void disableAll() {
        predator_.disable();
        openRange_.disable();
        stopRunFade_.disable();
        sessionHandoff_.disable();
        vwapDefense_.disable();
        liquidityVacuum_.disable();
    }
    
    // Reset all sessions
    void resetAllSessions() {
        predator_.resetSession();
        stopRunFade_.resetSession();
        vwapDefense_.resetSession();
        liquidityVacuum_.resetSession();
    }
    
    // Reset day (for day-limited profiles)
    void resetDay() {
        openRange_.resetDay();
        sessionHandoff_.resetDay();
    }
    
    // Check if any profile has position
    bool anyPositionOpen() const {
        return predator_.hasPosition() ||
               openRange_.hasPosition() ||
               stopRunFade_.hasPosition() ||
               sessionHandoff_.hasPosition() ||
               vwapDefense_.hasPosition() ||
               liquidityVacuum_.hasPosition();
    }
    
    // Count total trades across all profiles
    int totalTradesThisSession() const {
        return predator_.tradesThisSession() +
               stopRunFade_.tradesThisSession() +
               vwapDefense_.tradesThisSession() +
               liquidityVacuum_.tradesThisSession();
    }
    
    // Print all statuses
    void printAllStatus() const {
        printf("\n");
        printf("╔══════════════════════════════════════════════════════════════╗\n");
        printf("║           CHIMERA PROFILE MANAGER v4.9.0                     ║\n");
        printf("╚══════════════════════════════════════════════════════════════╝\n");
        
        predator_.printStatus();
        openRange_.printStatus();
        stopRunFade_.printStatus();
        sessionHandoff_.printStatus();
        vwapDefense_.printStatus();
        liquidityVacuum_.printStatus();
    }
    
    // Generate combined JSON
    void toJSON(char* buf, size_t buf_size) const {
        char pred[256], ore[256], srf[256], sh[256], vd[256], lv[256];
        
        predator_.toJSON(pred, sizeof(pred));
        openRange_.toJSON(ore, sizeof(ore));
        stopRunFade_.toJSON(srf, sizeof(srf));
        sessionHandoff_.toJSON(sh, sizeof(sh));
        vwapDefense_.toJSON(vd, sizeof(vd));
        liquidityVacuum_.toJSON(lv, sizeof(lv));
        
        snprintf(buf, buf_size,
            "{"
            "\"profiles\":["
            "%s,"
            "%s,"
            "%s,"
            "%s,"
            "%s,"
            "%s"
            "],"
            "\"any_position\":%s,"
            "\"total_trades\":%d"
            "}",
            pred, ore, srf, sh, vd, lv,
            anyPositionOpen() ? "true" : "false",
            totalTradesThisSession()
        );
    }

private:
    PredatorProfile predator_;
    OpenRangeProfile openRange_;
    StopRunFadeProfile stopRunFade_;
    SessionHandoffProfile sessionHandoff_;
    VwapDefenseProfile vwapDefense_;
    LiquidityVacuumProfile liquidityVacuum_;
};

// =============================================================================
// SINGLETON ACCESS
// =============================================================================
inline ProfileManager& getProfileManager() {
    static ProfileManager instance;
    return instance;
}

} // namespace Chimera
