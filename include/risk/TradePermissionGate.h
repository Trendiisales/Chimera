#pragma once
#include <cstdint>

enum class TradeBlockReason {
    NONE,
    SESSION_NOT_ARMED,
    VOLATILITY_SHOCK,
    SYMBOL_MUTED,
    REJECT_FUSE,
    IMPULSE_NOT_PERSISTENT,
    ASIA_DISABLED
};

struct TradeContext {
    const char* symbol;
    double impulse;
    double velocity;
    uint64_t now_ns;
};

class TradePermissionGate {
public:
    static TradePermissionGate& instance();
    
    bool allow(const TradeContext& ctx, TradeBlockReason& reason);
    
    void onReject(const char* symbol);
    void onFill(const char* symbol);
    void onSessionArm(const char* symbol);
    void onSessionDisarm(const char* symbol);
    void onVolatilityShock(const char* symbol, bool active);
    void onAsiaDisable(const char* symbol, bool disabled);

private:
    TradePermissionGate();
    
    struct SymbolState {
        bool session_armed = false;
        bool volatility_shock = false;
        bool asia_disabled = false;
        
        uint64_t mute_until_ns = 0;
        
        uint32_t rejects = 0;
        uint64_t reject_window_start_ns = 0;
        
        double last_impulse = 0.0;
        uint64_t impulse_start_ns = 0;
    };
    
    SymbolState& state(const char* symbol);
    
    bool impulsePersistent(SymbolState& s, double impulse, uint64_t now_ns);
    
    static constexpr uint64_t IMPULSE_PERSIST_NS = 400'000'000;
    static constexpr double IMPULSE_MIN = 0.08;
    
    static constexpr uint32_t REJECT_LIMIT = 10;
    static constexpr uint64_t REJECT_WINDOW_NS = 10'000'000'000;
    static constexpr uint64_t MUTE_NS = 60'000'000'000;
};

const char* tradeBlockReasonToString(TradeBlockReason reason);
