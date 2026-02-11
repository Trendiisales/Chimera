#include "XAUEntryGovernor.h"

static constexpr double IMPULSE_MIN = 0.08;
static constexpr double IMPULSE_ASIA_MIN = 0.12;

static constexpr uint64_t IMPULSE_PERSIST_NS = 400'000'000;
static constexpr uint64_t SHOCK_COOLDOWN_NS = 1'500'000'000;

static constexpr double ATR_PER_LEG = 2.5;
static constexpr int MAX_LEGS_HARD = 3;

XAUEntryGovernor::XAUEntryGovernor()
: impulse_ok_since_(0)
, cooldown_until_(0)
{}

EntryDecision XAUEntryGovernor::evaluate(const MarketState& m)
{
    // 1. HARD SESSION LOAD GATE
    if (!m.session_loaded) {
        return {false, "SESSION_NOT_READY"};
    }
    
    // 2. SHOCK HANDLING
    if (m.shock) {
        cooldown_until_ = m.now_ns + SHOCK_COOLDOWN_NS;
        impulse_ok_since_ = 0;
        return {false, "VOLATILITY_SHOCK"};
    }
    
    if (m.now_ns < cooldown_until_) {
        return {false, "SHOCK_COOLDOWN"};
    }
    
    // 3. IMPULSE FLOOR
    double min_impulse = m.asia_session ? IMPULSE_ASIA_MIN : IMPULSE_MIN;
    if (m.impulse < min_impulse) {
        impulse_ok_since_ = 0;
        return {false, "IMPULSE_TOO_WEAK"};
    }
    
    // 4. IMPULSE PERSISTENCE
    if (!impulse_persistent(m.impulse, m.now_ns)) {
        return {false, "IMPULSE_PERSISTENCE"};
    }
    
    // 5. ATR LEG GOVERNOR (NEVER ZERO)
    int max_legs = compute_max_legs(m.atr, m.asia_session);
    if (m.current_legs >= max_legs) {
        return {false, "ATR_LEG_LIMIT"};
    }
    
    return {true, "ENTRY_OK"};
}

bool XAUEntryGovernor::impulse_persistent(double impulse, uint64_t now_ns)
{
    if (impulse_ok_since_ == 0) {
        impulse_ok_since_ = now_ns;
        return false;
    }
    
    if (now_ns - impulse_ok_since_ >= IMPULSE_PERSIST_NS) {
        return true;
    }
    
    return false;
}

int XAUEntryGovernor::compute_max_legs(double atr, bool asia)
{
    int legs = int(atr / ATR_PER_LEG);
    
    // CRITICAL: NEVER ALLOW ZERO
    if (legs < 1)
        legs = 1;
    
    if (legs > MAX_LEGS_HARD)
        legs = MAX_LEGS_HARD;
    
    // Asia: single clean expansion leg only
    if (asia)
        legs = 1;
    
    return legs;
}
