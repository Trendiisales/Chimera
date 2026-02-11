#pragma once
#include <cstdint>
#include <algorithm>

struct MarketState {
    double impulse;
    double velocity;
    double atr;
    uint64_t now_ns;
    bool shock;
    bool asia_session;
    bool session_loaded;
    int current_legs;
};

struct EntryDecision {
    bool allow;
    const char* reason;
};

class XAUEntryGovernor {
public:
    XAUEntryGovernor();
    
    EntryDecision evaluate(const MarketState& m);

private:
    uint64_t impulse_ok_since_;
    uint64_t cooldown_until_;
    
    bool impulse_persistent(double impulse, uint64_t now_ns);
    int compute_max_legs(double atr, bool asia);
};
