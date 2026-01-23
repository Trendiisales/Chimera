#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace chimera {

struct SignalState {
    double ofi = 0;
    double impulse = 0;
    double funding = 0;
    double volatility = 0;
    double correlation = 0;
    double levels = 0;
};

struct TradeState {
    uint64_t id = 0;
    std::string time;
    std::string symbol;
    std::string engine;
    std::string side;
    double qty = 0;
    double entry = 0;
    double exit = 0;
    double pnl_bps = 0;
    double slippage_bps = 0;
    double latency_ms = 0;
    std::string regime;
    SignalState signals;
};

struct SymbolState {
    std::string symbol;
    uint32_t hash = 0;

    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    double spread_bps = 0.0;
    double depth = 0.0;

    std::string engine;
    std::string regime;

    double capital_weight = 1.0;
    bool enabled = true;

    // Legacy telemetry fields (TelemetryServer contract)
    double ofi = 0.0;
    double volatility = 0.0;
    double correlation = 0.0;

    // Phase A — Cost Gate telemetry
    double edge_bps = 0.0;
    double cost_bps = 0.0;
    double margin_bps = 0.0;
};

struct SystemState {
    std::string mode = "LIVE";
    std::string governor_mode = "OBSERVE";
    std::string build_id = "UNKNOWN";
    uint64_t uptime_s = 0;
    double clock_drift_ms = 0;
    bool kill_switch = false;
};

struct LatencyState {
    double tick_to_decision_ms = 0;
    double decision_to_send_ms = 0;
    double send_to_ack_ms = 0;
    double ack_to_fill_ms = 0;
    double rtt_total_ms = 0;
    double slippage_bps = 0;
    std::string venue = "BINANCE";
};

struct PnLState {
    double realized_bps = 0;
    double unrealized_bps = 0;
    double daily_dd_bps = 0;
    double risk_limit_bps = -20.0;
};

struct GovernorState {
    std::string recommendation = "HOLD";
    double confidence = 0;
    double survival_bps = 0;
    uint64_t cooldown_s = 0;
    std::string last_action = "NONE";
};

// GLOBAL STATE - Thread-safe telemetry store
struct GuiState {
    SystemState system;
    LatencyState latency;
    PnLState pnl;
    GovernorState governor;
    std::vector<SymbolState> symbols;
    std::vector<TradeState> trades;

    std::mutex mtx;
    
    // Singleton access
    static GuiState& instance() {
        static GuiState state;
        return state;
    }
};

}
