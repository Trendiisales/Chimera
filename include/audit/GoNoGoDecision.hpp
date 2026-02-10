// =============================================================================
// GoNoGoDecision.hpp - v4.8.0 - GO/NO-GO GATE DECISION STRUCTURE
// =============================================================================
// PURPOSE: Session start decision - trade or don't trade
//
// IF NO_GO:
//   - No profiles trade
//   - No overrides
//   - No "just one trade"
//
// PREVENTS:
//   - Revenge days
//   - Trading during decay
//   - Slow bleed weeks
//   - Operator interference
//
// OWNERSHIP: Jo
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

namespace Chimera {

enum class GoNoGoStatus : uint8_t {
    GO = 0,
    NO_GO = 1
};

inline const char* goNoGoStatusToString(GoNoGoStatus s) {
    switch (s) {
        case GoNoGoStatus::GO:    return "GO";
        case GoNoGoStatus::NO_GO: return "NO_GO";
        default:                  return "UNKNOWN";
    }
}

struct GoNoGoDecision {
    GoNoGoStatus status = GoNoGoStatus::NO_GO;

    std::string session;
    std::string reason;

    std::vector<std::string> blocking_profiles;
    std::vector<std::string> enabled_profiles;
    
    // =========================================================================
    // PRINT TO CONSOLE
    // =========================================================================
    void print() const {
        const char* icon = status == GoNoGoStatus::GO ? "🟢" : "🔴";
        
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║  GO / NO-GO DECISION                                          ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  Session: %-10s                                          ║\n", session.c_str());
        printf("║  Status:  %s %-8s                                         ║\n", 
               icon, goNoGoStatusToString(status));
        printf("║  Reason:  %-40s       ║\n", reason.c_str());
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        if (!enabled_profiles.empty()) {
            printf("║  Enabled:  ");
            for (size_t i = 0; i < enabled_profiles.size(); ++i) {
                printf("%s", enabled_profiles[i].c_str());
                if (i < enabled_profiles.size() - 1) printf(", ");
            }
            printf("\n");
        }
        
        if (!blocking_profiles.empty()) {
            printf("║  Blocked:  ");
            for (size_t i = 0; i < blocking_profiles.size(); ++i) {
                printf("%s", blocking_profiles[i].c_str());
                if (i < blocking_profiles.size() - 1) printf(", ");
            }
            printf("\n");
        }
        
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    // =========================================================================
    // JSON SERIALIZATION
    // =========================================================================
    void toJSON(char* buf, size_t buf_size) const {
        // Build blocking profiles string
        std::string blocking_str = "[";
        for (size_t i = 0; i < blocking_profiles.size(); ++i) {
            blocking_str += "\"" + blocking_profiles[i] + "\"";
            if (i < blocking_profiles.size() - 1) blocking_str += ",";
        }
        blocking_str += "]";
        
        // Build enabled profiles string
        std::string enabled_str = "[";
        for (size_t i = 0; i < enabled_profiles.size(); ++i) {
            enabled_str += "\"" + enabled_profiles[i] + "\"";
            if (i < enabled_profiles.size() - 1) enabled_str += ",";
        }
        enabled_str += "]";
        
        snprintf(buf, buf_size,
            "{"
            "\"type\":\"go_no_go\","
            "\"session\":\"%s\","
            "\"status\":\"%s\","
            "\"reason\":\"%s\","
            "\"blocking_profiles\":%s,"
            "\"enabled_profiles\":%s"
            "}",
            session.c_str(),
            goNoGoStatusToString(status),
            reason.c_str(),
            blocking_str.c_str(),
            enabled_str.c_str()
        );
    }
};

} // namespace Chimera
