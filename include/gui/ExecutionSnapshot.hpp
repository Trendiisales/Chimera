#pragma once
#include <string>
#include <map>
#include <cstdint>

namespace gui {

struct GateState {
    bool ok;
    std::string reason;
};

struct CostModel {
    double spread_bps = 0;
    double commission_bps = 0;
    double total_bps = 0;
};

struct EdgeModel {
    double raw_bps = 0;
    double latency_adj_bps = 0;
    double required_bps = 0;
};

struct ImpulseModel {
    double raw = 0;
    double latency_adj = 0;
    double min_required = 0;
};

struct PnLModel {
    double shadow = 0;
    double cash = 0;
};

struct SymbolSnapshot {
    double bid = 0;
    double ask = 0;
    double spread = 0;
    double latency_ms = 0;
    int trades = 0;
    int rejects = 0;
    int legs = 0;

    std::string session;
    std::string regime;
    std::string state;

    std::map<std::string, GateState> gates;
    CostModel cost;
    EdgeModel edge;
    ImpulseModel impulse;
    PnLModel pnl;
};

struct GovernorSnapshot {
    std::string daily_dd;
    std::string hourly_loss;
    std::string reject_rate;
    std::string action;
};

struct ConnectionSnapshot {
    bool fix = false;
    bool ctrader = false;
};

struct ExecutionSnapshot {
    uint64_t ts = 0;
    std::map<std::string, SymbolSnapshot> symbols;
    GovernorSnapshot governor;
    ConnectionSnapshot connections;
};

inline GateState Gate(bool ok, const std::string& pass, const std::string& fail) {
    return { ok, ok ? pass : fail };
}

std::string EmitJSON(const ExecutionSnapshot& s);

} // namespace gui
