// =============================================================================
// ProfileGovernor.hpp - v4.8.0 - PROFILE STATE MANAGEMENT
// =============================================================================
// PURPOSE: Auto-throttling and profile disabling based on audit results
//
// ENFORCEMENT (NON-NEGOTIABLE):
//   - FAIL verdict → Profile DISABLED
//   - WARNING verdict → Profile THROTTLED
//   - You cannot override this in live trading
//
// USAGE:
//   ProfileGovernor& gov = getProfileGovernor();
//   if (!gov.isAllowed("SCALP_FAST")) {
//       veto("PROFILE_DISABLED_BY_AUDIT");
//   }
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdio>

namespace Chimera {

enum class ProfileState : uint8_t {
    ENABLED = 0,
    THROTTLED = 1,   // Reduced size/frequency
    DISABLED = 2     // No trading allowed
};

inline const char* profileStateToString(ProfileState s) {
    switch (s) {
        case ProfileState::ENABLED:   return "ENABLED";
        case ProfileState::THROTTLED: return "THROTTLED";
        case ProfileState::DISABLED:  return "DISABLED";
        default:                      return "UNKNOWN";
    }
}

class ProfileGovernor {
public:
    // =========================================================================
    // STATE MANAGEMENT
    // =========================================================================
    void setState(const std::string& profile, ProfileState state) {
        std::lock_guard<std::mutex> lock(mutex_);
        ProfileState old_state = getStateInternal(profile);
        states_[profile] = state;
        
        if (old_state != state) {
            printf("[PROFILE-GOVERNOR] %s: %s → %s\n",
                   profile.c_str(),
                   profileStateToString(old_state),
                   profileStateToString(state));
        }
    }
    
    ProfileState getState(const std::string& profile) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return getStateInternal(profile);
    }
    
    // =========================================================================
    // ENTRY GATE CHECK
    // =========================================================================
    bool isAllowed(const std::string& profile) const {
        auto state = getState(profile);
        return state == ProfileState::ENABLED;
    }
    
    bool isThrottled(const std::string& profile) const {
        auto state = getState(profile);
        return state == ProfileState::THROTTLED;
    }
    
    bool isDisabled(const std::string& profile) const {
        auto state = getState(profile);
        return state == ProfileState::DISABLED;
    }
    
    // =========================================================================
    // THROTTLE MULTIPLIER (for size scaling)
    // =========================================================================
    double getThrottleMultiplier(const std::string& profile) const {
        auto state = getState(profile);
        switch (state) {
            case ProfileState::ENABLED:   return 1.0;
            case ProfileState::THROTTLED: return 0.5;  // 50% size
            case ProfileState::DISABLED:  return 0.0;  // No trading
            default:                      return 0.0;
        }
    }
    
    // =========================================================================
    // AUDIT ENFORCEMENT (CALLED AT END OF SESSION)
    // =========================================================================
    void applyAuditVerdict(const std::string& profile, const std::string& verdict) {
        if (verdict == "FAIL") {
            setState(profile, ProfileState::DISABLED);
            printf("[PROFILE-GOVERNOR] ❌ %s DISABLED due to FAIL verdict\n", profile.c_str());
        } else if (verdict == "WARNING") {
            setState(profile, ProfileState::THROTTLED);
            printf("[PROFILE-GOVERNOR] ⚠️ %s THROTTLED due to WARNING verdict\n", profile.c_str());
        } else {
            setState(profile, ProfileState::ENABLED);
        }
    }
    
    // =========================================================================
    // RESET (MANUAL OVERRIDE - REQUIRES EXPLICIT CALL)
    // =========================================================================
    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        states_.clear();
        printf("[PROFILE-GOVERNOR] All profiles reset to ENABLED\n");
    }
    
    void enableProfile(const std::string& profile) {
        setState(profile, ProfileState::ENABLED);
    }
    
    // =========================================================================
    // PRINT STATUS
    // =========================================================================
    void printStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║  PROFILE GOVERNOR STATUS                                      ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        if (states_.empty()) {
            printf("║  All profiles: ENABLED (default)                              ║\n");
        } else {
            for (const auto& [profile, state] : states_) {
                const char* icon = state == ProfileState::ENABLED ? "✅" :
                                   state == ProfileState::THROTTLED ? "⚠️" : "❌";
                printf("║  %-15s %s %-12s                              ║\n",
                       profile.c_str(), icon, profileStateToString(state));
            }
        }
        
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    // =========================================================================
    // SINGLETON
    // =========================================================================
    static ProfileGovernor& instance() {
        static ProfileGovernor inst;
        return inst;
    }

private:
    ProfileGovernor() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ProfileState> states_;
    
    ProfileState getStateInternal(const std::string& profile) const {
        auto it = states_.find(profile);
        if (it == states_.end())
            return ProfileState::ENABLED;
        return it->second;
    }
};

// =========================================================================
// CONVENIENCE FUNCTION
// =========================================================================
inline ProfileGovernor& getProfileGovernor() {
    return ProfileGovernor::instance();
}

} // namespace Chimera
