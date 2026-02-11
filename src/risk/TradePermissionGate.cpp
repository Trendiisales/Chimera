#include "risk/TradePermissionGate.h"
#include <unordered_map>
#include <string>
#include <iostream>

static std::unordered_map<std::string, TradePermissionGate::SymbolState> g_states;

TradePermissionGate& TradePermissionGate::instance() {
    static TradePermissionGate gate;
    return gate;
}

TradePermissionGate::TradePermissionGate() {}

TradePermissionGate::SymbolState& TradePermissionGate::state(const char* symbol) {
    return g_states[std::string(symbol)];
}

bool TradePermissionGate::impulsePersistent(SymbolState& s, double impulse, uint64_t now_ns) {
    if (impulse < IMPULSE_MIN) {
        s.impulse_start_ns = 0;
        s.last_impulse = impulse;
        return false;
    }
    
    if (s.impulse_start_ns == 0 || impulse < s.last_impulse) {
        s.impulse_start_ns = now_ns;
        s.last_impulse = impulse;
        return false;
    }
    
    s.last_impulse = impulse;
    return (now_ns - s.impulse_start_ns) >= IMPULSE_PERSIST_NS;
}

bool TradePermissionGate::allow(const TradeContext& ctx, TradeBlockReason& reason) {
    auto& s = state(ctx.symbol);
    
    if (!s.session_armed) {
        reason = TradeBlockReason::SESSION_NOT_ARMED;
        return false;
    }
    
    if (s.asia_disabled) {
        reason = TradeBlockReason::ASIA_DISABLED;
        return false;
    }
    
    if (s.volatility_shock) {
        reason = TradeBlockReason::VOLATILITY_SHOCK;
        return false;
    }
    
    if (ctx.now_ns < s.mute_until_ns) {
        reason = TradeBlockReason::SYMBOL_MUTED;
        return false;
    }
    
    if (!impulsePersistent(s, ctx.impulse, ctx.now_ns)) {
        reason = TradeBlockReason::IMPULSE_NOT_PERSISTENT;
        return false;
    }
    
    reason = TradeBlockReason::NONE;
    return true;
}

void TradePermissionGate::onReject(const char* symbol) {
    auto& s = state(symbol);
    uint64_t now = s.reject_window_start_ns;
    
    if (now == 0 || s.rejects == 0) {
        s.reject_window_start_ns = now;
        s.rejects = 1;
        return;
    }
    
    s.rejects++;
    
    if (s.rejects >= REJECT_LIMIT) {
        s.mute_until_ns = now + MUTE_NS;
        s.rejects = 0;
        s.reject_window_start_ns = 0;
        std::cout << "[MUTE] " << symbol << " (reject fuse)\n";
    }
}

void TradePermissionGate::onFill(const char* symbol) {
    auto& s = state(symbol);
    s.rejects = 0;
    s.reject_window_start_ns = 0;
}

void TradePermissionGate::onSessionArm(const char* symbol) {
    state(symbol).session_armed = true;
    std::cout << "[GATE] " << symbol << " SESSION_ARMED\n";
}

void TradePermissionGate::onSessionDisarm(const char* symbol) {
    state(symbol).session_armed = false;
}

void TradePermissionGate::onVolatilityShock(const char* symbol, bool active) {
    state(symbol).volatility_shock = active;
    if (active) {
        std::cout << "[GATE] " << symbol << " VOLATILITY_SHOCK\n";
    }
}

void TradePermissionGate::onAsiaDisable(const char* symbol, bool disabled) {
    state(symbol).asia_disabled = disabled;
    if (disabled) {
        std::cout << "[GATE] " << symbol << " ASIA_DISABLED\n";
    }
}

const char* tradeBlockReasonToString(TradeBlockReason reason) {
    switch(reason) {
        case TradeBlockReason::NONE: return "NONE";
        case TradeBlockReason::SESSION_NOT_ARMED: return "SESSION_NOT_ARMED";
        case TradeBlockReason::VOLATILITY_SHOCK: return "VOLATILITY_SHOCK";
        case TradeBlockReason::SYMBOL_MUTED: return "SYMBOL_MUTED";
        case TradeBlockReason::REJECT_FUSE: return "REJECT_FUSE";
        case TradeBlockReason::IMPULSE_NOT_PERSISTENT: return "IMPULSE_NOT_PERSISTENT";
        case TradeBlockReason::ASIA_DISABLED: return "ASIA_DISABLED";
    }
    return "UNKNOWN";
}
