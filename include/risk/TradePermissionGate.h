#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

struct TradeContext {
    const char* symbol;
    double impulse;
    uint64_t now_ns;
};

enum class TradeBlockReason {
    NONE,
    SESSION_NOT_ARMED,
    VOLATILITY_SHOCK,
    SYMBOL_MUTED,
    REJECT_FUSE,
    IMPULSE_NOT_PERSISTENT,
    ASIA_DISABLED
};

class TradePermissionGate {
public:
    struct SymbolState {
        int rejects = 0;
        uint64_t reject_window_start_ns = 0;
        uint64_t mute_until_ns = 0;

        bool session_armed = false;
        bool volatility_shock = false;
        bool asia_disabled = false;

        double last_impulse = 0.0;
        uint64_t impulse_start_ns = 0;
    };

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

    SymbolState& state(const char* symbol);
    bool impulsePersistent(SymbolState& s, double impulse, uint64_t now_ns);

    std::unordered_map<std::string, SymbolState> states_;

    static constexpr double IMPULSE_MIN = 0.25;
    static constexpr uint64_t IMPULSE_PERSIST_NS = 50'000'000;
    static constexpr int REJECT_LIMIT = 5;
    static constexpr uint64_t MUTE_NS = 5'000'000'000ULL;
};

const char* tradeBlockReasonToString(TradeBlockReason reason);
