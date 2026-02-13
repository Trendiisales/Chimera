#pragma once
// Stub for ExecutionAuthority - V1 SHADOW mode bypasses all checks

namespace Chimera {

enum class ExecBlockReason {
    NONE,
    INCOME_LOCKED,
    NAS100_OWNERSHIP,
    RISK_LIMIT,
    OTHER
};

inline const char* execBlockReasonToString(ExecBlockReason reason) {
    switch (reason) {
        case ExecBlockReason::NONE: return "NONE";
        case ExecBlockReason::INCOME_LOCKED: return "INCOME_LOCKED";
        case ExecBlockReason::NAS100_OWNERSHIP: return "NAS100_OWNERSHIP";
        case ExecBlockReason::RISK_LIMIT: return "RISK_LIMIT";
        case ExecBlockReason::OTHER: return "OTHER";
        default: return "UNKNOWN";
    }
}

class ExecutionAuthority {
public:
    static ExecutionAuthority& instance() {
        static ExecutionAuthority inst;
        return inst;
    }

    // Always allow in SHADOW mode
    bool allowCFD(const char* symbol, bool fix_connected, bool expansion, bool intent_live, ExecBlockReason* reason) {
        if (reason) *reason = ExecBlockReason::NONE;
        return true;
    }

private:
    ExecutionAuthority() = default;
};

inline ExecutionAuthority& getExecutionAuthority() {
    return ExecutionAuthority::instance();
}

} // namespace Chimera
