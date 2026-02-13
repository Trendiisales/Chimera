#pragma once
#include <atomic>
#include <cstdio>
#include <cstring>

namespace Chimera {

enum class EngineId {
    UNKNOWN,
    INCOME,
    CFD
};

inline const char* engine_id_str(EngineId id) {
    switch (id) {
        case EngineId::INCOME: return "INCOME";
        case EngineId::CFD: return "CFD";
        default: return "UNKNOWN";
    }
}

struct NAS100OwnershipState {
    int current_owner = 0;
    bool income_window_active = false;
    bool cfd_no_new_entries = false;
    int ny_hour = 0;
    int ny_minute = 0;
    int seconds_to_income_window = 0;
    int seconds_in_income_window = 0;
    int cfd_forced_flat_seconds = 0;
};

class GlobalRiskGovernor {
public:
    static GlobalRiskGovernor& instance() {
        static GlobalRiskGovernor inst;
        return inst;
    }

    bool canSubmitOrder(EngineId engine) { return true; }
    double sizeMultiplier(EngineId engine) const { return 1.0; }
    double maxRiskNZD(EngineId engine) const { return 1000.0; }
    void onTradeComplete(EngineId engine, double pnl_nzd) {}

    void toJSON(char* buf, size_t len) const {
        snprintf(buf, len, "{\"status\":\"ok\"}");
    }

private:
    GlobalRiskGovernor() = default;
};

inline bool canSubmitOrder(EngineId engine) {
    return GlobalRiskGovernor::instance().canSubmitOrder(engine);
}

inline double sizeMultiplier(EngineId engine) {
    return GlobalRiskGovernor::instance().sizeMultiplier(engine);
}

inline double maxRiskNZD(EngineId engine) {
    return GlobalRiskGovernor::instance().maxRiskNZD(engine);
}

inline void onTradeComplete(EngineId engine, double pnl_nzd) {
    GlobalRiskGovernor::instance().onTradeComplete(engine, pnl_nzd);
}

inline bool canTradeNAS100(EngineId engine) { return true; }

inline NAS100OwnershipState getNAS100OwnershipState() {
    return NAS100OwnershipState();
}

class EngineOwnership {
public:
    static EngineOwnership& instance() {
        static EngineOwnership inst;
        return inst;
    }
    bool isIncomeLocked() const { return false; }
};

} // namespace Chimera
