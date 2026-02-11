#pragma once
#include "core/TradeLedger.hpp"
#include "execution/ExecutionGovernor.hpp"
#include "execution/ExecutionRouter.hpp"
#include "risk/LatencyAwareTP.h"
#include "risk/ImpulseSizer.h"
#include "risk/ImpulseProfitGovernor.h"
#include "routing/SymbolOpportunityRouter.hpp"
#include <vector>
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace shadow {

enum class ExecMode { LIVE, SHADOW };
enum class Side { BUY, SELL, FLAT };
enum class Metal { XAU, XAG };

struct Signal {
    Side side;
    double price;
    double confidence;
};

struct Tick {
    double bid;
    double ask;
    uint64_t ts_ms;
};

struct Leg {
    Side side;
    double size;
    double entry;
    double stop;
    double take_profit;
    double entry_impulse;
    uint64_t entry_ts;
};

struct SymbolConfig {
    std::string symbol;
    double base_size;
    double initial_stop;
    double initial_tp;
    int max_legs;
};

struct SessionGuard {
    uint64_t session_close_utc;
    uint64_t flatten_buffer_sec;
    uint64_t liquidity_fade_sec;
};

struct RejectionStats {
    uint64_t total_rejections = 0;
    uint64_t dd_rejects = 0;
    uint64_t edge_rejects = 0;
    uint64_t latency_rejects = 0;
};

using GUITradeCallback = std::function<void(const char*, uint64_t, char, double, double, double, double, uint64_t)>;
using ExitCallback = std::function<void(const char*, uint64_t, double, double, const char*)>;

class SymbolExecutor {
public:
    SymbolExecutor(const SymbolConfig& cfg, ExecMode mode, ExecutionRouter& router);
    
    void onTick(const Tick& t);
    void onSignal(const Signal& s, uint64_t ts_ms);
    void setGUICallback(GUITradeCallback cb);
    void setExitCallback(ExitCallback cb);
    double getRealizedPnL() const;
    double getLastBid() const { return last_bid_; }
    double getLastAsk() const { return last_ask_; }
    double getSpread() const { return last_ask_ - last_bid_; }
    double getLatencyMs() const { return last_latency_ms_; }
    const std::vector<Leg>& getLegs() const { return legs_; }
    int getActiveLegs() const;
    int getTradesThisHour() const { return trades_this_hour_; }
    int getTotalRejections() const { return rejection_stats_.total_rejections; }
    void status() const;

private:
    SymbolConfig cfg_;
    ExecMode mode_;
    TradeLedger ledger_;
    ExecutionGovernor governor_;
    SessionGuard session_guard_;
    Metal metal_type_;
    
    ExecutionRouter& router_;
    ImpulseProfitGovernor profit_governor_;
    
    GUITradeCallback gui_callback_;
    ExitCallback exit_callback_;
    std::vector<Leg> legs_;
    std::unordered_map<size_t, uint64_t> leg_to_trade_;
    double realized_pnl_;
    RejectionStats rejection_stats_;
    uint64_t last_entry_ts_;
    uint32_t trades_this_hour_;
    uint64_t hour_start_ts_;
    double last_bid_;
    double last_ask_;
    double last_latency_ms_;
    double account_equity_;
    
    bool canEnter(const Signal& s, uint64_t ts_ms);
    void enterBase(Side side, double price, uint64_t ts);
    void enterFromEngine(Side side, double price, double size, const char* engine, uint64_t ts);
    void exitAll(const char* reason, double price, uint64_t ts);
};

} // namespace shadow
