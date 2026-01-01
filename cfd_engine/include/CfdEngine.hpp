// =============================================================================
// CfdEngine.hpp - cTrader FIX Trading Engine
// =============================================================================
// v4.7.0: INTENT-BASED EXECUTION GATING (THE PRIME DIRECTIVE)
//        - Added IntentGate check as GUARD 0 (before all other checks)
//        - No execution unless INTENT == LIVE
//        - Session detector with NY expansion detection
//        - Symbol policy enforcement (pre-FIX rules)
//        - Execution replay logging for decision analysis
//        - Standby mode when no edge expected
// v3.11: XAGUSD min lot = 50, HFT params fixed, STATIC VARIABLE AUDIT COMPLETE
//        - Removed all mutable static variables (was causing cross-symbol corruption!)
//        - Added per-symbol SymbolDiag struct for diagnostic counters
// v3.10: ASYMMETRIC TP/SL (TP=10-15bps, SL=-3-5bps) + CSV trade logging
// v3.6: Production-clean logging - removed all verbose debug output
// v3.5: Fixed XAUUSD/XAGUSD min_trade_size = 1.0 for BlackBull demo
// v6.72: Relaxed trade thresholds + autostart support
// v6.80: Added PnL to order callback for session tracking
// v6.85: Integrated MicroStateMachine for anti-churn logic
//        - Impulse gating prevents noise trading
//        - Direction lock prevents flip-flopping
//        - Churn detection auto-disables toxic symbols
// v6.97 FIXES:
//   - Added symbol enable/disable filtering (checks TradingConfig)
//   - Fixed PnL calculation: proper currency conversion (not 1:1 bps)
//   - Added latency tracking per tick
// =============================================================================
#pragma once

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <iostream>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <mutex>

#include "EngineTypes.hpp"
#include "IntentQueue.hpp"
#include "market/TickFull.hpp"
#include "data/UnifiedTick.hpp"
#include "micro/CentralMicroEngine.hpp"
#include "micro/MicroEngines_CRTP.hpp"
#include "strategy/Strategies_Bucket.hpp"
#include "risk/RiskGuardian.hpp"
#include "execution/SmartExecutionEngine.hpp"
#include "fix/CTraderFIXClient.hpp"
#include "strategy/PureScalper.hpp"

// NEW: MarketState classification
#include "shared/MarketState.hpp"
#include "shared/TradingConfig.hpp"
#include "bringup/BringUpSystem.hpp"

// v4.5.0: Engine-level symbol ownership enforcement
#include "core/EngineOwnership.hpp"

// v4.5.1: Global Risk Governor - execution-layer enforcement
#include "shared/GlobalRiskGovernor.hpp"

// v4.6.0: Speed-optimised thresholds and metrics
#include "speed/SpeedOptimizedThresholds.hpp"
#include "speed/SpeedEdgeMetrics.hpp"

// v3.0: Shadow trading + Expectancy Authority
#include "../crypto_engine/include/risk/ExpectancyAuthority.hpp"

// v4.7.0: Intent-based execution gating (THE PRIME DIRECTIVE)
#include "shared/IntentGate.hpp"
#include "shared/SymbolPolicy.hpp"
#include "shared/SessionDetector.hpp"
#include "shared/ExecutionReplay.hpp"
#include "shared/IntentEnforcer.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace Omega {

// =============================================================================
// DEBUG FILE LOGGER - Writes EVERYTHING to chimera_debug.log
// =============================================================================
class DebugLogger {
    std::ofstream file_;
    std::mutex mutex_;
    bool enabled_ = true;
    
public:
    DebugLogger() {
        file_.open("chimera_debug.log", std::ios::out | std::ios::app);
        if (file_.is_open()) {
            log("=== CHIMERA DEBUG LOG STARTED ===");
        }
    }
    
    ~DebugLogger() {
        if (file_.is_open()) {
            log("=== CHIMERA DEBUG LOG ENDED ===");
            file_.close();
        }
    }
    
    void log(const char* msg) {
        if (!enabled_ || !file_.is_open()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        file_ << std::put_time(std::localtime(&time), "%H:%M:%S") 
              << "." << std::setfill('0') << std::setw(3) << ms.count()
              << " | " << msg << "\n";
        file_.flush();
    }
    
    void log(const std::string& msg) { log(msg.c_str()); }
    
    template<typename... Args>
    void logf(const char* fmt, Args... args) {
        if (!enabled_ || !file_.is_open()) return;
        char buf[512];
        if constexpr (sizeof...(args) == 0) {
            snprintf(buf, sizeof(buf), "%s", fmt);
        } else {
            snprintf(buf, sizeof(buf), fmt, args...);
        }
        log(buf);
    }
    
    void logTick(const char* symbol, double bid, double ask) {
        logf("TICK %s bid=%.5f ask=%.5f spread=%.5f", symbol, bid, ask, ask-bid);
    }
    
    void logSignal(const char* symbol, int dir, double conf, const char* reason) {
        logf("SIGNAL %s dir=%d conf=%.2f reason=%s", symbol, dir, conf, reason);
    }
    
    void logBlock(const char* symbol, const char* reason) {
        logf("BLOCK %s reason=%s", symbol, reason);
    }
    
    void logTrade(const char* action, const char* symbol, double price, double size) {
        logf("TRADE %s %s price=%.5f size=%.4f", action, symbol, price, size);
    }
    
    void logConnection(const char* what, bool connected) {
        logf("CONNECTION %s = %s", what, connected ? "CONNECTED" : "DISCONNECTED");
    }
    
    void logError(const char* msg) {
        logf("ERROR: %s", msg);
    }
};

// Global debug logger instance
inline DebugLogger& getDebugLog() {
    static DebugLogger instance;
    return instance;
}

#define DBG_LOG(msg) getDebugLog().log(msg)
#define DBG_LOGF(...) getDebugLog().logf(__VA_ARGS__)
#define DBG_TICK(sym, bid, ask) getDebugLog().logTick(sym, bid, ask)
#define DBG_SIGNAL(sym, dir, conf, reason) getDebugLog().logSignal(sym, dir, conf, reason)
#define DBG_BLOCK(sym, reason) getDebugLog().logBlock(sym, reason)
#define DBG_TRADE(action, sym, price, size) getDebugLog().logTrade(action, sym, price, size)
#define DBG_CONN(what, connected) getDebugLog().logConnection(what, connected)
#define DBG_ERROR(msg) getDebugLog().logError(msg)

using Chimera::CTraderFIXClient;
using Chimera::CTraderTick;
using Chimera::CTraderExecReport;
using Chimera::FIXConfig;
using Chimera::MarketState;
using Chimera::TradeIntent;
using Chimera::MarketStateSnapshot;
using Chimera::MarketStateClassifier;

// BringUp visibility types
using Chimera::SuppressionEvent;
using Chimera::SuppressionLayer;
using Chimera::SuppressionReason;
using Chimera::getBringUpManager;

// =============================================================================
// CfdEngine Statistics
// =============================================================================
struct CfdEngineStats {
    std::atomic<uint64_t> ticks_processed{0};
    std::atomic<uint64_t> signals_generated{0};
    std::atomic<uint64_t> orders_sent{0};
    std::atomic<uint64_t> orders_filled{0};
    std::atomic<uint64_t> total_latency_ns{0};
    std::atomic<uint64_t> max_latency_ns{0};
    std::atomic<uint64_t> fix_messages{0};
    std::atomic<uint64_t> fix_reconnects{0};
    std::atomic<uint64_t> vetoed_signals{0};
    std::atomic<uint64_t> state_gated{0};  // NEW: trades blocked by market state
    std::atomic<uint64_t> buy_votes{0};
    std::atomic<uint64_t> sell_votes{0};
    std::atomic<uint64_t> consensus_trades{0};
    
    double avgLatencyUs() const {
        uint64_t ticks = ticks_processed.load(std::memory_order_relaxed);
        if (ticks == 0) return 0.0;
        return static_cast<double>(total_latency_ns.load(std::memory_order_relaxed)) / ticks / 1000.0;
    }
};

// =============================================================================
// v3.0: CFD Shadow Trading State (per-symbol)
// =============================================================================
struct CfdShadowState {
    bool position_open = false;
    double entry_price = 0.0;
    uint64_t entry_ts = 0;
    int side = 0;  // +1 long, -1 short
    uint64_t trades_total = 0;
    
    // Expectancy tracking for this symbol
    Chimera::Risk::ExpectancyAuthority authority;
    
    // Tier determines behavior
    // TIER 1: Full shadow + can promote to live
    // TIER 2: Shadow only, conservative
    // TIER 3: Sensor - shadow for data, no live ever
    // TIER 4: Disabled - no shadow, no live
    int tier = 4;  // Default disabled
    
    CfdShadowState() : authority(Chimera::Risk::ExpectancyAuthority::Config()) {}
};

// =============================================================================
// CfdEngine - cTrader FIX Trading Engine with MarketState Integration
// =============================================================================
class CfdEngine {
public:
    static constexpr int CPU_CORE = 2;
    
    CfdEngine() 
        : running_(false), connected_(false), kill_switch_(nullptr), execEngine_(centralMicro_)
    {
        DBG_LOG("CfdEngine constructor called");
        // v4.2.2: Expanded symbol universe
        forex_symbols_ = {"EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCAD", "NZDUSD", "USDCHF", "EURGBP"};
        metals_symbols_ = {"XAUUSD", "XAGUSD"};
        indices_symbols_ = {"US30", "US100", "NAS100", "SPX500", "GER40"};
        
        // Configure market state classifier thresholds
        stateClassifier_.config().vpin_max = 0.60;
        stateClassifier_.config().spread_max_bps = 10.0;
        
        // v4.2.2: Revised tier assignments for more trading opportunities
        // TIER 1 LIVE: Ready for real trades (none yet - need proven expectancy)
        
        // TIER 2 CONDITIONAL: Active shadow trading, can promote to live
        // v4.5.0: NAS100 moved to INCOME engine - not traded by CFD engine
        shadow_state_["XAUUSD"].tier = 2;   // Gold - primary defensive
        // shadow_state_["NAS100"].tier = 2;   // DISABLED v4.5.0: owned by IncomeEngine
        shadow_state_["US100"].tier = 2;    // Nasdaq alias
        shadow_state_["US30"].tier = 2;     // v4.6.0: Dow - speed-optimised NY only
        shadow_state_["SPX500"].tier = 2;   // v4.6.0: S&P - momentum-only NY only
        shadow_state_["EURUSD"].tier = 2;   // Major FX - very liquid
        shadow_state_["GBPUSD"].tier = 2;   // Cable
        
        // TIER 3 SENSOR: Shadow for data, learning
        shadow_state_["XAGUSD"].tier = 3;   // Silver
        shadow_state_["USDJPY"].tier = 3;   // Yen
        shadow_state_["AUDUSD"].tier = 3;   // Aussie
        shadow_state_["GER40"].tier = 3;    // DAX
        shadow_state_["NAS100"].tier = 3;   // v4.5.0: Sensor only - owned by IncomeEngine
        
        // TIER 4 DISABLED: No shadow, no live (can enable later)
        shadow_state_["USDCAD"].tier = 4;
        shadow_state_["USDCHF"].tier = 4;
        shadow_state_["NZDUSD"].tier = 4;
        shadow_state_["EURGBP"].tier = 4;
        
        std::cout << "[CFD-SHADOW] v4.6.0 Tier assignments (speed-optimised indices):\n"
                  << "  TIER 2 (ACTIVE): XAUUSD, US100, US30, SPX500, EURUSD, GBPUSD\n"
                  << "  TIER 3 (SENSOR): XAGUSD, USDJPY, AUDUSD, GER40, NAS100\n"
                  << "  TIER 4 (DISABLED): USDCAD, USDCHF, NZDUSD, EURGBP\n";
        
        // v4.7.0: Log intent-based execution policy (THE PRIME DIRECTIVE)
        std::cout << "\n[CFD-ENGINE] v4.7.0 INTENT-BASED EXECUTION POLICY:\n"
                  << "  ════════════════════════════════════════════════════════════\n"
                  << "  🔒 THE PRIME DIRECTIVE: No execution unless INTENT == LIVE\n"
                  << "  ════════════════════════════════════════════════════════════\n"
                  << "  PRE-FIX ALLOWED: BTCUSDT (crypto), XAUUSD (NY expansion only)\n"
                  << "  PRE-FIX DISABLED: NAS100, US30, SPX500, all indices\n"
                  << "  PROBES: Disabled for all CFD symbols\n"
                  << "  SHADOW: Allowed for policy-compliant symbols only\n"
                  << "  ════════════════════════════════════════════════════════════\n\n";
    }
    
    ~CfdEngine() { stop(); }
    
    void setFIXConfig(const FIXConfig& cfg) { fixConfig_ = cfg; }
    void setForexSymbols(const std::vector<std::string>& s) { forex_symbols_ = s; }
    void setMetalsSymbols(const std::vector<std::string>& s) { metals_symbols_ = s; }
    void setIndicesSymbols(const std::vector<std::string>& s) { indices_symbols_ = s; }
    void setKillSwitch(GlobalKillSwitch* ks) { kill_switch_ = ks; }
    void setOrderCallback(std::function<void(const char*, int8_t, double, double, double)> cb) { orderCallback_ = std::move(cb); }
    
    // v4.7.0: Intent state for ExecutionAuthority
    void setIntentLive(bool live) noexcept { fixClient_.setIntentLive(live); }
    bool isIntentLive() const noexcept { return fixClient_.isIntentLive(); }
    void setNYExpansion(bool active) noexcept { fixClient_.setNYExpansion(active); }
    
    // v7.08: Print per-symbol FIX tick stats
    void printSymbolTickStats() const {
        fixClient_.printSymbolTickStats();
    }
    
    // PnL callback: symbol, pnl_value, is_close (true when position closes)
    void setPnLCallback(std::function<void(const char*, double, bool)> cb) { pnlCallback_ = std::move(cb); }
    void setTickCallback(std::function<void(const char*, double, double, double, double, double, double)> cb) { tickCallback_ = std::move(cb); }
    
    // NEW: Market state callback for GUI
    void setMarketStateCallback(std::function<void(MarketState, TradeIntent, int, const char*)> cb) { 
        marketStateCallback_ = std::move(cb); 
    }
    
    void setBucketCallback(std::function<void(int, int, int8_t, bool, const char*)> cb) {
        bucketCallback_ = std::move(cb);
    }
    
    void setBucketWeights(const BucketWeights& w) { stratPack_.aggregator.setWeights(w); }
    
    // v4.5.1: Check if any position is open (for cross-engine coordination)
    bool hasPosition() const noexcept {
        for (const auto& [symbol, shadow] : shadow_state_) {
            if (shadow.position_open) return true;
        }
        return false;
    }
    
    bool start() {
        if (running_) return false;
        running_ = true;
        riskGuard_.start();
        execEngine_.start();
        engineThread_ = std::thread(&CfdEngine::engineLoop, this);
        pinToCpu(engineThread_, CPU_CORE);
        std::cout << "[CfdEngine] Started on CPU " << CPU_CORE << " with MarketState gating" << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_.load()) return;
        std::cout << "[CfdEngine] Stop requested..." << std::endl;
        running_.store(false);
        connected_.store(false);
        fixClient_.disconnect();
        if (engineThread_.joinable()) {
            std::atomic<bool> joined{false};
            std::thread joiner([this, &joined]() { engineThread_.join(); joined.store(true); });
            for (int i = 0; i < 30 && !joined.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (joined.load()) joiner.join();
            else { std::cerr << "[CfdEngine] Thread join timeout, detaching\n"; joiner.detach(); }
        }
        execEngine_.stop();
        riskGuard_.stop();
        std::cout << "[CfdEngine] Stopped. Ticks: " << stats_.ticks_processed.load() << std::endl;
    }
    
    // v3.0: Shadow state getters for GUI
    const CfdShadowState* getShadowState(const char* symbol) const {
        auto it = shadow_state_.find(symbol);
        return (it != shadow_state_.end()) ? &it->second : nullptr;
    }
    
    double getShadowExpectancy(const char* symbol) const {
        auto it = shadow_state_.find(symbol);
        return (it != shadow_state_.end()) ? it->second.authority.fast_expectancy() : 0.0;
    }
    
    int getShadowTrades(const char* symbol) const {
        auto it = shadow_state_.find(symbol);
        return (it != shadow_state_.end()) ? it->second.authority.fast_trades() : 0;
    }
    
    int getSymbolTier(const char* symbol) const {
        auto it = shadow_state_.find(symbol);
        return (it != shadow_state_.end()) ? it->second.tier : 4;
    }

    // =========================================================================
    // HOT PATH - v6.72 PURE SCALPER MODE
    // Simple price-action scalping - no complex bucket voting
    // v6.97 FIX: Added symbol enable check
    // =========================================================================
    inline void processTick(const TickFull& tick) {
        if (kill_switch_ && kill_switch_->isCfdKilled()) return;
        
        // v7.02: Symbol enable check moved to trade execution section
        // All ticks flow through for GUI display, only trading is filtered
        
        uint64_t start_ns = nowNs();
        stats_.ticks_processed.fetch_add(1, std::memory_order_relaxed);
        
        // Convert to UnifiedTick for GUI/micro
        UnifiedTick ut;
        convertTick(tick, ut);
        
        // Update central microstructure engine (for GUI display)
        centralMicro_.onTick(ut);
        const MicrostructureSignals& signals = centralMicro_.getSignals();
        
        // v3.11 FIX: Per-symbol diagnostic tracking (was static - shared across all symbols!)
        // Note: Using string key for simplicity, could use symbol hash for performance
        std::string sym_key(tick.symbol);
        auto& diag = symbol_diag_[sym_key];  // Creates entry if missing
        
        bool is_diag_symbol = (strcmp(tick.symbol, "US30") == 0 || 
                               strcmp(tick.symbol, "XAUUSD") == 0 ||
                               strcmp(tick.symbol, "EURUSD") == 0);
        bool should_log_periodic = is_diag_symbol && (++diag.counter % 200 == 0);  // Every 200 ticks instead of 500
        
        // =====================================================================
        // PURE SCALPER - Simple price-action trading
        // =====================================================================
        // v6.99: Set contract_size for proper currency PnL calculation
        CfdSymbolMeta meta = getSymbolMeta(tick.symbol);
        auto& scalp_cfg = scalper_.getConfig();
        scalp_cfg.contract_size = meta.contract_size;
        
        ScalpSignal scalp = scalper_.process(
            tick.symbol, 
            tick.bid, tick.ask, 
            tick.bid_size, tick.ask_size,
            start_ns
        );
        
        // v3.11: Per-symbol reason tracking
        bool reason_changed = (strcmp(scalp.reason, diag.last_reason.c_str()) != 0);
        uint64_t now_ms = start_ns / 1000000;
        bool enough_time_passed = (now_ms - diag.last_reason_time) > 1000;  // At least 1 second between logs
        
        if (reason_changed && enough_time_passed) {
            DBG_LOGF("REASON_CHANGE %s: %s -> %s", tick.symbol, diag.last_reason.c_str(), scalp.reason);
            diag.last_reason = scalp.reason;
            diag.last_reason_time = now_ms;
        }
        
        // =====================================================================
        // v4.7.0: UPDATE SESSION DETECTOR AND INTENT STATE
        // =====================================================================
        double mid = (tick.bid + tick.ask) / 2.0;
        double spread_bps = (tick.ask - tick.bid) / mid * 10000.0;
        
        // Update session detector with tick metrics
        Chimera::IntentEnforcer::updateSessionMetrics(
            tick.symbol, mid, tick.bid_size, tick.ask_size, start_ns);
        
        // Calculate edge/conviction from scalper state
        const auto* state_for_intent = scalper_.getState(tick.symbol);
        double edge = 0.0;
        double conviction = 0.0;
        
        // Regime stability from MarketState classifier (DEAD = unstable, VOLATILE = unstable)
        bool regime_stable = (currentState_.state == MarketState::TRENDING || 
                              currentState_.state == MarketState::RANGING);
        
        if (state_for_intent) {
            // Edge based on trend strength and momentum
            edge = std::abs(state_for_intent->momentum) * 0.5;
            if (state_for_intent->trend() != 0) edge += 0.3;
            if (state_for_intent->ticks > 20) edge += 0.1;
            edge = std::min(1.0, edge);
            
            // Conviction based on consistency
            conviction = state_for_intent->ticks > 10 ? 0.5 : 0.3;
            if (scalp.shouldTrade()) conviction += 0.3;
            conviction = std::min(1.0, conviction);
        }
        
        // Update intent state machine
        Chimera::IntentState current_intent = Chimera::IntentEnforcer::updateIntent(
            tick.symbol, edge, conviction, regime_stable, start_ns);
        
        // =====================================================================
        // v4.7.0: STANDBY DETECTION
        // =====================================================================
        if (Chimera::IntentEnforcer::shouldEnterStandby(start_ns) && 
            !Chimera::IntentEnforcer::isStandby()) {
            Chimera::IntentEnforcer::enterStandby(start_ns);
        }
        
        // Update GUI with scalper state
        int buyVotes = (scalp.direction > 0) ? 1 : 0;
        int sellVotes = (scalp.direction < 0) ? 1 : 0;
        int8_t consensus = scalp.direction;
        
        if (bucketCallback_) {
            bucketCallback_(buyVotes, sellVotes, consensus, false, scalp.reason);
        }
        
        // Broadcast tick to GUI
        if (tickCallback_) {
            double latency_ms = stats_.avgLatencyUs() / 1000.0;  // Convert μs to ms
            tickCallback_(tick.symbol, ut.bid, ut.ask, 
                          signals.orderFlowImbalance, signals.vpin, signals.toxicity, latency_ms);
        }
        
        // Track votes for stats
        if (buyVotes > 0) stats_.buy_votes.fetch_add(1, std::memory_order_relaxed);
        if (sellVotes > 0) stats_.sell_votes.fetch_add(1, std::memory_order_relaxed);
        
        // Market state for GUI (simplified - always show RANGING with MEAN_REVERSION)
        MarketState mktState = MarketState::RANGING;
        TradeIntent intent = TradeIntent::MEAN_REVERSION;
        int conviction_gui = scalp.shouldTrade() ? 7 : 4;
        
        // v4.7.0: Include intent state in reason for GUI
        char intent_reason[128];
        snprintf(intent_reason, sizeof(intent_reason), "%s [%s]", 
                 scalp.reason, Chimera::intent_state_str(current_intent));
        
        if (marketStateCallback_) {
            marketStateCallback_(mktState, intent, conviction_gui, intent_reason);
        }
        
        // Diagnostic output - v6.83: Updated for new PureScalper API
        if (should_log_periodic) {
            const auto* state = scalper_.getState(tick.symbol);
            std::cout << "\n[SCALP-" << tick.symbol << "] "
                      << "bid=" << tick.bid << " ask=" << tick.ask 
                      << " spread=" << (tick.ask - tick.bid);
            if (state) {
                std::cout << " trend=" << (int)state->trend()
                          << " momentum=" << state->momentum
                          << " spreadBps=" << state->spreadBps()
                          << " ticks=" << state->ticks;
                if (state->pos.active) {
                    double pnlBps = state->pos.pnlBps(state->mid);
                    std::cout << " POS=" << (state->pos.side > 0 ? "LONG" : "SHORT")
                              << " pnlBps=" << pnlBps;
                }
            }
            std::cout << " REASON=" << scalp.reason << "\n";
            
            // Also log to debug file
            DBG_LOGF("PERIODIC %s: reason=%s dir=%d conf=%.2f", 
                     tick.symbol, scalp.reason, scalp.direction, scalp.confidence);
        }
        
        // =====================================================================
        // v3.0: CFD SHADOW TRADING (bootstraps expectancy)
        // v4.7.0: Respects symbol policy for shadow trading
        // =====================================================================
        std::string sym_str(tick.symbol);
        auto& shadow = shadow_state_[sym_str];
        
        // v4.7.0: Check if shadow trading is allowed for this symbol
        bool shadow_policy_ok = Chimera::IntentEnforcer::canShadowTrade(tick.symbol, spread_bps);
        
        // Only process shadow for TIER 2 and TIER 3 symbols that pass policy check
        if (shadow.tier >= 2 && shadow.tier <= 3 && shadow_policy_ok) {
            double mid = (tick.bid + tick.ask) / 2.0;
            double spread_bps = (tick.ask - tick.bid) / mid * 10000.0;
            
            // Shadow spread caps (relaxed vs live) - per asset class
            double shadow_max_spread = 8.0;  // Default forex
            if (sym_str == "XAUUSD") shadow_max_spread = 5.0;
            else if (sym_str == "XAGUSD") shadow_max_spread = 12.0;
            else if (sym_str == "NAS100" || sym_str == "SPX500") shadow_max_spread = 4.0;
            else if (sym_str == "US30") shadow_max_spread = 6.0;
            
            bool shadow_spread_ok = spread_bps <= shadow_max_spread && spread_bps > 0.1;
            
            // v3.0 FIX: Use RAW trend signal, not filtered scalp signal
            // PureScalper blocks on spread first, zeroing direction - shadow must bypass this
            const auto* state = scalper_.getState(tick.symbol);
            int8_t raw_direction = state ? state->trend() : 0;
            bool shadow_signal_ok = raw_direction != 0 && state && state->ticks > 10;
            
            // v3.6-clean: Removed verbose shadow debug logging for production
            
            // Open shadow position
            if (shadow_spread_ok && shadow_signal_ok && !shadow.position_open) {
                shadow.position_open = true;
                shadow.entry_price = mid;
                shadow.entry_ts = start_ns;
                shadow.side = raw_direction;  // Use raw trend, not scalp.direction
                shadow.trades_total++;
                
                std::cout << "[CFD-SHADOW-OPEN] " << tick.symbol
                          << (shadow.side > 0 ? " LONG" : " SHORT")
                          << " @ " << std::fixed << std::setprecision(4) << mid
                          << " spread=" << std::setprecision(1) << spread_bps << "bps"
                          << " tier=" << shadow.tier
                          << " (shadow #" << shadow.trades_total << ")\n";
            }
            
            // Check shadow position exit
            if (shadow.position_open) {
                double shadow_pnl_bps = 0.0;
                if (shadow.side > 0) {
                    shadow_pnl_bps = (mid - shadow.entry_price) / shadow.entry_price * 10000.0;
                } else {
                    shadow_pnl_bps = (shadow.entry_price - mid) / shadow.entry_price * 10000.0;
                }
                
                uint64_t hold_ms = (start_ns - shadow.entry_ts) / 1000000;
                
                // v3.10: ASYMMETRIC TP/SL - let winners run, cut losers FAST
                // With 35% win rate, need TP ~3x SL for positive expectancy
                // Win: 35% × 10bps = 3.5bps | Loss: 65% × 3bps = 1.95bps | Net: +1.55bps
                double shadow_tp = 10.0, shadow_sl = -3.0;     // Forex: wide TP, tight SL
                uint64_t shadow_max_hold = 8000;               // 8 seconds
                
                // Asset-specific
                if (sym_str == "XAUUSD") { shadow_tp = 12.0; shadow_sl = -4.0; shadow_max_hold = 10000; }
                else if (sym_str == "XAGUSD") { shadow_tp = 15.0; shadow_sl = -5.0; shadow_max_hold = 10000; }
                else if (sym_str == "NAS100" || sym_str == "SPX500" || sym_str == "US30") { 
                    shadow_tp = 8.0; shadow_sl = -3.0; shadow_max_hold = 8000;
                }
                
                bool should_exit = (shadow_pnl_bps >= shadow_tp) ||
                                  (shadow_pnl_bps <= shadow_sl) ||
                                  (hold_ms >= shadow_max_hold);
                
                if (should_exit) {
                    shadow.position_open = false;
                    
                    // Feed PnL to expectancy authority
                    shadow.authority.record(shadow_pnl_bps);
                    
                    const char* exit_reason = 
                        (shadow_pnl_bps >= shadow_tp) ? "TP" :
                        (shadow_pnl_bps <= shadow_sl) ? "SL" : "TIME";
                    
                    std::cout << "[CFD-SHADOW-CLOSE] " << tick.symbol
                              << (shadow.side > 0 ? " LONG" : " SHORT")
                              << " PnL=" << std::fixed << std::setprecision(2) << shadow_pnl_bps << "bps"
                              << " hold=" << hold_ms << "ms"
                              << " exit=" << exit_reason
                              << " | E=" << shadow.authority.fast_expectancy() << "bps"
                              << " (" << shadow.authority.fast_trades() << "t)\n";
                    
                    // v3.10: Log to CSV for analysis
                    // v4.5.0: Pass engine_id for attribution
                    logShadowTradeCSV(Chimera::EngineId::CFD, tick.symbol, shadow.side > 0 ? "LONG" : "SHORT",
                                      shadow.entry_price, mid, shadow_pnl_bps, 
                                      hold_ms, exit_reason, shadow.trades_total);
                }
            }
        }
        
        // =====================================================================
        // EXECUTE TRADE (LIVE)
        // =====================================================================
        // v7.02 FIX: Check symbol enabled HERE (not at start) so GUI still gets updates
        const auto* sym_cfg = Chimera::getTradingConfig().getSymbolConfig(tick.symbol);
        bool symbol_enabled = sym_cfg && sym_cfg->enabled;
        
        // v7.03 DEBUG: Log trade signals with enable status
        if (scalp.shouldTrade()) {
            std::cout << "[TRADE-CHECK] " << tick.symbol 
                      << " enabled=" << (symbol_enabled ? "YES" : "NO")
                      << " connected=" << (connected_ ? "YES" : "NO")
                      << " -> " << (symbol_enabled && connected_ ? "EXECUTE" : "BLOCKED") << "\n";
        }
        
        if (scalp.shouldTrade() && connected_ && symbol_enabled) {
            DBG_SIGNAL(tick.symbol, scalp.direction, scalp.confidence, scalp.reason);
            // Risk check
            if (!riskGuard_.checkOrder(scalp.size, scalp.direction)) {
                std::cout << "  [RISK] Order blocked by RiskGuard\n";
                DBG_BLOCK(tick.symbol, "RISK_GUARD");
            } else {
                // v6.80: Pass PnL with order for GUI tracking
                // v4.5.0: Pass engine ID for ownership enforcement
                double trade_pnl = scalp.is_exit ? scalp.realized_pnl : 0.0;
                submitOrder(Chimera::EngineId::CFD, tick.symbol, scalp.direction, scalp.size, (tick.bid + tick.ask) / 2.0, trade_pnl);
                stats_.consensus_trades.fetch_add(1, std::memory_order_relaxed);
                
                // Report PnL on exits
                if (scalp.is_exit && pnlCallback_) {
                    pnlCallback_(tick.symbol, scalp.realized_pnl, true);
                }
                
                DBG_TRADE(scalp.direction > 0 ? "BUY" : "SELL", tick.symbol, 
                         (tick.bid + tick.ask) / 2, scalp.size);
                
                std::cout << "[SCALP-TRADE] " << tick.symbol 
                          << (scalp.direction > 0 ? " BUY " : " SELL ")
                          << scalp.size << " @ " << (tick.bid + tick.ask) / 2
                          << " reason=" << scalp.reason;
                if (scalp.is_exit) {
                    std::cout << " PnL=" << trade_pnl;
                }
                std::cout << "\n";
            }
        } else if (scalp.shouldTrade() && !connected_) {
            std::cout << "  [WARN] Trade signal but FIX not connected\n";
            DBG_BLOCK(tick.symbol, "FIX_DISCONNECTED");
        } else if (scalp.shouldTrade() && !symbol_enabled) {
            // v7.02: Symbol disabled - don't trade but don't log (too spammy)
        }
        
        // Update latency stats
        uint64_t latency = nowNs() - start_ns;
        stats_.total_latency_ns.fetch_add(latency, std::memory_order_relaxed);
        uint64_t max_lat = stats_.max_latency_ns.load(std::memory_order_relaxed);
        if (latency > max_lat) stats_.max_latency_ns.store(latency, std::memory_order_relaxed);
        
        if (scalp.direction != 0) {
            stats_.signals_generated.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // Accessors
    const CfdEngineStats& getStats() const { return stats_; }
    const MicrostructureSignals& getSignals() const { return centralMicro_.getSignals(); }
    const BucketWeights& getBucketWeights() const { return stratPack_.aggregator.getWeights(); }
    const MarketStateSnapshot& getMarketState() const { return currentState_; }
    bool isRunning() const { return running_; }
    bool isConnected() const { return connected_; }

private:
    void engineLoop() {
        std::cout << "[CfdEngine] Loop started" << std::endl;
        std::cout << "[CfdEngine] Connecting to FIX: " << fixConfig_.host << std::endl;
        DBG_LOG("CfdEngine loop started");
        DBG_LOGF("FIX Config: host=%s quote_port=%d trade_port=%d", 
                 fixConfig_.host.c_str(), fixConfig_.pricePort, fixConfig_.tradePort);
        
        // Setup FIX callbacks
        fixClient_.setConfig(fixConfig_);
        fixClient_.setOnTick([this](const CTraderTick& t) { onFIXMarketData(t); });
        fixClient_.setOnExec([this](const CTraderExecReport& r) { onFIXExecution(r); });
        fixClient_.setOnLatency([this](const std::string& symbol, double rtt_ms, double slippage_bps) {
            onFIXLatency(symbol, rtt_ms, slippage_bps);
        });
        
        int reconnectAttempts = 0;
        const int maxBackoffSec = 60;
        
        while (running_) {
            if (!connected_) {
                reconnectAttempts++;
                int backoffSec = std::min(5 * reconnectAttempts, maxBackoffSec);
                
                std::cout << "[CfdEngine] Reconnect attempt #" << reconnectAttempts << std::endl;
                DBG_LOGF("RECONNECT attempt #%d backoff=%ds", reconnectAttempts, backoffSec);
                stats_.fix_reconnects.fetch_add(1, std::memory_order_relaxed);
                fixClient_.disconnect();
                
                for (int i = 0; i < 20 && running_; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!running_) break;
                
                DBG_LOG("Calling fixClient_.connect()...");
                if (fixClient_.connect()) {
                    connected_ = true;
                    reconnectAttempts = 0;
                    std::cout << "[CfdEngine] FIX connected" << std::endl;
                    DBG_CONN("FIX", true);
                    
                    fixClient_.requestSecurityList();
                    int waitCount = 0;
                    while (!fixClient_.isSecurityListReady() && waitCount < 300 && running_) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        waitCount++;
                    }
                    
                    if (fixClient_.isSecurityListReady()) {
                        DBG_LOG("Security list ready, subscribing symbols");
                        for (const auto& sym : forex_symbols_) fixClient_.subscribeMarketData(sym);
                        for (const auto& sym : metals_symbols_) fixClient_.subscribeMarketData(sym);
                        for (const auto& sym : indices_symbols_) fixClient_.subscribeMarketData(sym);
                        DBG_LOG("Subscribed to all symbols");
                    } else {
                        DBG_ERROR("Security list timeout!");
                    }
                } else {
                    DBG_ERROR("fixClient_.connect() returned false");
                    DBG_CONN("FIX", false);
                    for (int i = 0; i < (backoffSec * 10) && running_; ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } else {
                // Connected - process pending intents
                processIntents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        DBG_LOG("Engine loop stopped");
        std::cout << "[CfdEngine] Loop stopped" << std::endl;
    }
    
    void onFIXMarketData(const CTraderTick& t) {
        stats_.fix_messages.fetch_add(1, std::memory_order_relaxed);
        
        // v3.11 FIX: Per-symbol tick count (was static - shared across all symbols!)
        auto& diag = symbol_diag_[t.symbol];
        if ((++diag.fix_tick_count % 1000) == 1) {
            DBG_TICK(t.symbol.c_str(), t.bid, t.ask);
        }
        
        TickFull tick{};  // Zero-initialize all fields
        std::strncpy(tick.symbol, t.symbol.c_str(), 15);
        tick.symbol[15] = '\0';
        tick.venue = Venue::CTRADER;
        tick.ts_ns = nowNs();
        tick.bid = t.bid;
        tick.ask = t.ask;
        tick.bid_size = t.bidSize;
        tick.ask_size = t.askSize;
        tick.flags = TICK_FLAG_BBO_UPDATE;
        
        // =====================================================================
        // VOLUME PROXY FROM BID/ASK SIZE CHANGES
        // When bid size decreases, someone bought (lifted the ask) 
        // When ask size decreases, someone sold (hit the bid)
        // This is how real HFT systems infer trade flow from L2 data
        // =====================================================================
        auto it = lastSizes_.find(t.symbol);
        if (it != lastSizes_.end()) {
            double bidDelta = it->second.first - t.bidSize;   // Positive = bid was consumed
            double askDelta = it->second.second - t.askSize;  // Positive = ask was consumed
            
            // If bid was consumed (decreased), that's selling pressure
            // If ask was consumed (decreased), that's buying pressure
            if (askDelta > 0) {
                tick.buy_vol = askDelta;   // Buyers lifting asks
            }
            if (bidDelta > 0) {
                tick.sell_vol = bidDelta;  // Sellers hitting bids
            }
        }
        lastSizes_[t.symbol] = {t.bidSize, t.askSize};
        
        processTick(tick);
    }
    
    void onFIXExecution(const CTraderExecReport& r) {
        std::cout << "[CfdEngine] Exec: " << r.clOrdID << " status=" << r.ordStatus << std::endl;
        if (r.isFill()) {
            stats_.orders_filled.fetch_add(1, std::memory_order_relaxed);
            DBG_LOGF("FILL clOrdID=%s status=%c symbol=%s side=%c qty=%.4f price=%.5f",
                     r.clOrdID.c_str(), r.ordStatus, r.symbol.c_str(), 
                     r.side, r.lastQty, r.lastPx);
        } else {
            DBG_LOGF("EXEC clOrdID=%s status=%c", r.clOrdID.c_str(), r.ordStatus);
        }
    }
    
    // v4.2.2: Latency callback - updates per-symbol SymbolState latency
    void onFIXLatency(const std::string& symbol, double rtt_ms, double slippage_bps) {
        auto* st = scalper_.getSymbolState(symbol.c_str());
        if (st) {
            st->latency.update(rtt_ms, slippage_bps);
            std::cout << "[CfdEngine] Latency updated: " << symbol 
                      << " ema_rtt=" << st->latency.ema_rtt_ms << "ms"
                      << " samples=" << st->latency.sample_count << "\n";
        }
    }
    
    inline void convertTick(const TickFull& src, UnifiedTick& dst) {
        std::strncpy(dst.symbol, src.symbol, 15);
        dst.symbol[15] = '\0';
        dst.bid = src.bid; dst.ask = src.ask;
        dst.spread = src.spread();
        dst.bidSize = src.bid_size; dst.askSize = src.ask_size;
        dst.buyVol = src.buy_vol; dst.sellVol = src.sell_vol;
        dst.tsLocal = src.ts_ns; dst.tsExchange = src.ts_exchange;
        dst.b1 = src.bid_depth[0]; dst.b2 = src.bid_depth[1];
        dst.b3 = src.bid_depth[2]; dst.b4 = src.bid_depth[3]; dst.b5 = src.bid_depth[4];
        dst.a1 = src.ask_depth[0]; dst.a2 = src.ask_depth[1];
        dst.a3 = src.ask_depth[2]; dst.a4 = src.ask_depth[3]; dst.a5 = src.ask_depth[4];
        dst.computeDepth();
    }
    
    inline void updateMicroEngines(const UnifiedTick& t) {
        micro01_.onTick(t); micro02_.onTick(t); micro03_.onTick(t); micro04_.onTick(t);
        micro05_.onTick(t); micro06_.onTick(t); micro07_.onTick(t); micro08_.onTick(t);
        micro09_.onTick(t); micro10_.onTick(t); micro11_.onTick(t); micro12_.onTick(t);
        micro13_.onTick(t); micro14_.onTick(t); micro15_.onTick(t); micro16_.onTick(t);
        micro17_.onTick(t);
    }
    
    // =========================================================================
    // CFD SYMBOL METADATA - v6.88 FIX
    // =========================================================================
    struct CfdSymbolMeta {
        double min_trade_size = 0.01;
        double contract_size = 100.0;
        double tick_size = 0.01;
        bool is_index = false;
    };
    
    CfdSymbolMeta getSymbolMeta(const char* symbol) const {
        CfdSymbolMeta meta;
        std::string sym(symbol);
        
        // XAUUSD / Gold - BlackBull demo requires 1.0 lot minimum (100 oz/lot)
        if (sym == "XAUUSD" || sym == "GOLD") {
            meta.min_trade_size = 1.0;   // v3.5: Fixed for BlackBull demo 1.0 minimum
            meta.contract_size = 100.0;  // 100 oz per lot
            meta.tick_size = 0.01;
            meta.is_index = false;
        }
        // XAGUSD / Silver - BlackBull demo requires 50.0 lot minimum
        else if (sym == "XAGUSD" || sym == "SILVER") {
            meta.min_trade_size = 50.0;   // v3.11: BlackBull demo 50.0 minimum (error said min=50)
            meta.contract_size = 5000.0; // 5000 oz per lot
            meta.tick_size = 0.001;
            meta.is_index = false;
        }
        // Indices - REQUIRE WHOLE LOTS
        else if (sym == "NAS100" || sym == "US100" || sym == "US30" || sym == "SPX500" || sym == "UK100" || sym == "GER40") {
            meta.min_trade_size = 1.0;  // Indices need 1.0 minimum
            meta.contract_size = 1.0;
            meta.tick_size = 0.25;
            meta.is_index = true;
        }
        // FX pairs
        else if (sym.length() == 6 || sym.find("USD") != std::string::npos || sym.find("EUR") != std::string::npos) {
            meta.min_trade_size = 0.01;
            meta.contract_size = 100000.0;
            meta.tick_size = 0.00001;
            meta.is_index = false;
        }
        
        return meta;
    }
    
    // =========================================================================
    // v4.5.0: Submit order with ENGINE ID for ownership enforcement
    // Engine ID must be passed by caller - no default, no hardcoding
    // =========================================================================
    
    // DELETE the old signature to prevent accidental use without EngineId
    // This is a compile-time enforcement - if anyone tries to call without
    // EngineId, they get a compiler error, not a runtime bug
    void submitOrder(const char*, int8_t, double, double, double = 0.0) = delete;
    
    inline void submitOrder(Chimera::EngineId engine_id, const char* symbol, int8_t side, 
                            double qty, double price, double pnl = 0.0) {
        // =====================================================================
        // v4.7.0 THE PRIME DIRECTIVE - INTENT GATE CHECK (GUARD 0)
        // =====================================================================
        // NO ORDER MAY BE SENT UNLESS INTENT == LIVE
        // This is checked FIRST, before everything else.
        // =====================================================================
        uint64_t now_ns = nowNs();
        double spread_bps = 0.0;
        
        // Get spread from scalper state if available
        const auto* scalp_state_check = scalper_.getState(symbol);
        if (scalp_state_check) {
            spread_bps = scalp_state_check->spreadBps();
        }
        
        // THE INTENT CHECK - This single line would have prevented all probe trades
        auto intent_result = Chimera::checkExecution(engine_id, symbol, spread_bps, now_ns);
        if (!intent_result.allowed) {
            DBG_LOGF("ORDER_BLOCKED symbol=%s reason=%s intent=%s",
                     symbol, Chimera::block_reason_str(intent_result.reason),
                     Chimera::intent_state_str(intent_result.intent));
            std::cout << "[EXEC-BLOCKED] engine=" << Chimera::engine_id_str(engine_id)
                      << " symbol=" << symbol
                      << " BLOCKED - " << Chimera::block_reason_str(intent_result.reason)
                      << " (intent=" << Chimera::intent_state_str(intent_result.intent) << ")\n";
            return;  // HARD STOP - intent not LIVE
        }
        
        // =====================================================================
        // v4.5.1 HARD EXECUTION GUARDS (NON-NEGOTIABLE - CHECKED FIRST)
        // These guards are at the execution boundary - NOTHING bypasses them
        // =====================================================================
        
        // GUARD 1: DAILY LOSS HARD STOP (-$200 NZD)
        // If tripped, NO orders go through - period.
        if (!Chimera::GlobalRiskGovernor::instance().canSubmitOrder(engine_id)) {
            DBG_LOGF("ORDER_BLOCKED symbol=%s reason=RISK_GOVERNOR", symbol);
            std::cout << "[EXEC-BLOCKED] engine=" << Chimera::engine_id_str(engine_id)
                      << " symbol=" << symbol 
                      << " BLOCKED - RISK GOVERNOR (daily loss or throttle)\n";
            return;  // HARD STOP - nothing passes
        }
        
        // GUARD 2: NAS100 TIME-BASED OWNERSHIP
        // This is THE critical guard that prevents engine overlap on NAS100
        if (strcmp(symbol, "NAS100") == 0 && !Chimera::canTradeNAS100(engine_id)) {
            DBG_LOGF("ORDER_BLOCKED symbol=NAS100 reason=OWNERSHIP_VIOLATION");
            std::cout << "[EXEC-BLOCKED] engine=" << Chimera::engine_id_str(engine_id)
                      << " symbol=NAS100"
                      << " BLOCKED - ownership violation (not your window)\n";
            return;  // HARD STOP - wrong engine for NAS100 right now
        }
        
        // =====================================================================
        // v4.6.0 SPEED-OPTIMISED EXECUTION GUARDS (US30, SPX500, etc.)
        // =====================================================================
        const auto& speed_thresholds = Chimera::Speed::getSpeedThresholds(symbol);
        
        // GUARD 3: INDEX CFD SESSION CHECK (US30, SPX500 - NY only)
        std::string sym_str(symbol);
        if ((sym_str == "US30" || sym_str == "SPX500") && 
            !Chimera::canTradeIndexCFD(engine_id, sym_str)) {
            DBG_LOGF("ORDER_BLOCKED symbol=%s reason=NOT_NY_SESSION", symbol);
            std::cout << "[EXEC-BLOCKED] engine=" << Chimera::engine_id_str(engine_id)
                      << " symbol=" << symbol
                      << " BLOCKED - not NY session\n";
            return;
        }
        
        // GUARD 4: ENGINE OWNERSHIP ENFORCEMENT (symbol allowlist)
        // Check this BEFORE latency/spread gates (risk/ownership first)
        if (!Chimera::EngineOwnership::instance().isAllowedWithLog(engine_id, symbol)) {
            DBG_LOGF("ORDER_BLOCKED symbol=%s reason=ENGINE_OWNERSHIP", symbol);
            std::cout << "[ENGINE-BLOCK] engine=" << Chimera::engine_id_str(engine_id)
                      << " symbol=" << symbol 
                      << " BLOCKED - not in allowed list\n";
            return;
        }
        
        // GUARD 5: LATENCY GATE (HARD BLOCK)
        // Get current latency from PureScalper state
        const auto* scalp_state = scalper_.getState(symbol);
        double current_latency_ms = scalp_state ? scalp_state->latency.ema_rtt_ms : 5.0;
        
        if (current_latency_ms > speed_thresholds.latency_block_ms && 
            speed_thresholds.latency_block_ms > 0.0) {
            DBG_LOGF("ORDER_BLOCKED symbol=%s reason=LATENCY lat=%.2fms limit=%.2fms",
                     symbol, current_latency_ms, speed_thresholds.latency_block_ms);
            std::cout << "[EXEC-BLOCKED] engine=" << Chimera::engine_id_str(engine_id)
                      << " symbol=" << symbol
                      << " BLOCKED - latency " << current_latency_ms 
                      << "ms > " << speed_thresholds.latency_block_ms << "ms\n";
            Chimera::Speed::SpeedEdgeMetrics::instance().onLatencyUpdate(current_latency_ms);
            return;
        }
        
        // Update latency metrics
        Chimera::Speed::SpeedEdgeMetrics::instance().onLatencyUpdate(current_latency_ms);
        
        // =====================================================================
        // v6.88 EXECUTION PATH DEBUG + SIZE FLOORING
        // =====================================================================
        CfdSymbolMeta meta = getSymbolMeta(symbol);
        double original_size = qty;
        double final_size = qty;
        
        // v4.6.0: Apply size multiplier from speed thresholds
        if (speed_thresholds.max_size_mult_vs_nas < 1.0 && speed_thresholds.tier > 1) {
            final_size *= speed_thresholds.max_size_mult_vs_nas;
            std::cout << "[SIZE_SCALE] " << symbol << " tier=" << speed_thresholds.tier
                      << " mult=" << speed_thresholds.max_size_mult_vs_nas
                      << " -> " << final_size << "\n";
        }
        
        // v4.6.0: Apply latency degradation (50% size if latency > allow but < block)
        double lat_mult = speed_thresholds.getLatencySizeMultiplier(current_latency_ms);
        if (lat_mult < 1.0 && lat_mult > 0.0) {
            final_size *= lat_mult;
            std::cout << "[SIZE_LAT_DEGRADE] " << symbol << " lat=" << current_latency_ms
                      << "ms mult=" << lat_mult << " -> " << final_size << "\n";
        }
        
        // FLOOR TO MIN TRADE SIZE
        if (final_size < meta.min_trade_size) {
            final_size = meta.min_trade_size;
            std::cout << "[SIZE_FLOOR] " << symbol << " raw=" << original_size 
                      << " floored to min=" << final_size << "\n";
        }
        
        // ROUND INDICES TO WHOLE LOTS
        if (meta.is_index) {
            final_size = std::round(final_size);
            if (final_size < 1.0) final_size = 1.0;
            std::cout << "[INDEX_ROUND] " << symbol << " rounded to " << final_size << "\n";
        }
        
        // LOG EXECUTION CHECK (v4.5.0: now includes engine_id)
        std::cout << "[EXEC_CHECK] engine=" << Chimera::engine_id_str(engine_id)
                  << " symbol=" << symbol 
                  << " side=" << (side > 0 ? "BUY" : "SELL")
                  << " raw=" << original_size
                  << " final=" << final_size
                  << " min=" << meta.min_trade_size
                  << " price=" << price << "\n";
        
        // RISK CHECK WITH DEBUG
        if (!riskGuard_.checkOrder(final_size, side)) {
            DBG_LOGF("ORDER_BLOCKED symbol=%s reason=RISK_GUARD event=%d",
                     symbol, static_cast<int>(riskGuard_.getLastEvent()));
            std::cout << "[EXEC_VETO] " << symbol << " RISK_GUARD_BLOCKED"
                      << " event=" << static_cast<int>(riskGuard_.getLastEvent()) << "\n";
            return;
        }
        
        // CALLBACK (v4.5.0: log includes engine attribution)
        if (orderCallback_) orderCallback_(symbol, side, final_size, price, pnl);
        
        // QUEUE INTENT
        Intent::Side iSide = (side > 0) ? Intent::BUY : Intent::SELL;
        Intent intent(iSide, symbol, final_size, nowNs());
        intentQueue_.push(intent);
        stats_.orders_sent.fetch_add(1, std::memory_order_relaxed);
        
        // LOG TO FILE (persistent)
        DBG_LOGF("ORDER_QUEUED engine=%s symbol=%s side=%s size=%.4f price=%.5f",
                 Chimera::engine_id_str(engine_id), symbol, 
                 (side > 0 ? "BUY" : "SELL"), final_size, price);
        
        std::cout << "[EXEC_QUEUED] engine=" << Chimera::engine_id_str(engine_id)
                  << " symbol=" << symbol 
                  << " " << (side > 0 ? "BUY" : "SELL") 
                  << " " << final_size << " @ " << price << "\n";
    }
    
    inline void processIntents() {
        Intent intent;
        while (intentQueue_.try_pop(intent)) {
            std::cout << "[INTENT_POP] " << intent.symbol 
                      << " " << (intent.side == Intent::BUY ? "BUY" : "SELL")
                      << " qty=" << intent.qty 
                      << " connected=" << connected_.load() << "\n";
            
            if (connected_) {
                char fixSide = (intent.side == Intent::BUY) ? Chimera::FIXSide::Buy : Chimera::FIXSide::Sell;
                
                std::cout << "[FIX_SEND] " << intent.symbol 
                          << " side=" << fixSide
                          << " qty=" << intent.qty << "\n";
                
                bool sent = fixClient_.sendMarketOrder(intent.symbol, fixSide, intent.qty);
                
                std::cout << "[FIX_RESULT] " << intent.symbol 
                          << " sent=" << (sent ? "YES" : "NO") << "\n";
                
                if (sent) {
                    // Record clean fill for promotion tracking
                    getBringUpManager().recordCleanFill(intent.symbol, "CFD", 0.5, 0.0);
                }
            } else {
                std::cout << "[INTENT_BLOCKED] " << intent.symbol << " FIX_DISCONNECTED\n";
                
                // FIX not connected - emit suppression event
                SuppressionEvent evt;
                evt.timestamp_ns = nowNs();
                evt.setSymbol(intent.symbol);
                evt.setVenue("CFD");
                evt.setStrategy("Execution");
                evt.intent_direction = (intent.side == Intent::BUY) ? 1 : -1;
                evt.base_size = intent.qty;
                evt.final_size = 0.0;
                evt.layer = SuppressionLayer::EXEC;
                evt.reason = SuppressionReason::FIX_NOT_LIVE;
                evt.setVenueHealth("RED");
                evt.setFixState("DISCONNECTED");
                evt.bring_up_enabled = true;
                getBringUpManager().logSuppression(evt);
                getBringUpManager().recordFault(intent.symbol, "CFD", SuppressionReason::FIX_NOT_LIVE);
            }
        }
    }
    
    static void pinToCpu(std::thread& t, int cpu) {
#ifdef __linux__
        cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(cpu, &cpuset);
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
#else
        (void)t; (void)cpu;
#endif
    }
    
    inline uint64_t nowNs() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    GlobalKillSwitch* kill_switch_;
    std::thread engineThread_;
    
    FIXConfig fixConfig_;
    mutable CTraderFIXClient fixClient_;  // mutable for const printSymbolTickStats()
    
    std::vector<std::string> forex_symbols_;
    std::vector<std::string> metals_symbols_;
    std::vector<std::string> indices_symbols_;
    
    CentralMicroEngine centralMicro_;
    MarketStateClassifier stateClassifier_;
    MarketStateSnapshot currentState_;
    
    // Baseline values for normalization (EMA updated)
    // NOTE: Start with reasonable baseline for forex/metals
    // XAUUSD volatility typically 2-10 per day, EURUSD ~50-100 pips
    double medianSpreadBps_ = 2.0;
    double baselineVol_ = 5.0;  // FIXED: Was 0.0005 which caused vol_z = 12000+
    
    // Track bid/ask sizes for volume inference
    std::unordered_map<std::string, std::pair<double, double>> lastSizes_;
    
    // v3.11: Per-symbol diagnostic tracking (was static - caused cross-symbol corruption!)
    struct SymbolDiag {
        uint64_t counter = 0;
        uint64_t fix_tick_count = 0;
        std::string last_reason = "";
        uint64_t last_reason_time = 0;
    };
    std::unordered_map<std::string, SymbolDiag> symbol_diag_;
    
    MicroEngine01 micro01_; MicroEngine02 micro02_; MicroEngine03 micro03_; MicroEngine04 micro04_;
    MicroEngine05 micro05_; MicroEngine06 micro06_; MicroEngine07 micro07_; MicroEngine08 micro08_;
    MicroEngine09 micro09_; MicroEngine10 micro10_; MicroEngine11 micro11_; MicroEngine12 micro12_;
    MicroEngine13 micro13_; MicroEngine14 micro14_; MicroEngine15 micro15_; MicroEngine16 micro16_;
    MicroEngine17 micro17_;
    
    StrategyPack stratPack_;
    PureScalper scalper_;  // v6.72: Pure scalping strategy
    IntentQueue<1024> intentQueue_;
    RiskGuardian riskGuard_;
    SmartExecutionEngine execEngine_;
    CfdEngineStats stats_;
    std::function<void(const char*, int8_t, double, double, double)> orderCallback_;  // v6.80: Added PnL param
    std::function<void(const char*, double, bool)> pnlCallback_;  // PnL callback
    std::function<void(const char*, double, double, double, double, double, double)> tickCallback_;
    std::function<void(MarketState, TradeIntent, int, const char*)> marketStateCallback_;
    std::function<void(int, int, int8_t, bool, const char*)> bucketCallback_;
    
    // v3.0: Shadow trading state per symbol
    std::unordered_map<std::string, CfdShadowState> shadow_state_;
    
    // v3.10: CSV logging for trade analysis
    std::ofstream shadow_csv_;
    bool csv_initialized_ = false;
    
    void initShadowCSV() {
        if (csv_initialized_) return;
        shadow_csv_.open("cfd_shadow_trades.csv", std::ios::out | std::ios::trunc);
        if (shadow_csv_.is_open()) {
            shadow_csv_ << "timestamp,engine,symbol,side,entry_price,exit_price,pnl_bps,hold_ms,exit_reason,trade_num\n";
            shadow_csv_.flush();
            csv_initialized_ = true;
            std::cout << "[CSV] Trade logging initialized: cfd_shadow_trades.csv (v4.5.0 with engine_id)\n";
        }
    }
    
    void logShadowTradeCSV(Chimera::EngineId engine_id, const char* symbol, const char* side, 
                           double entry, double exit, double pnl_bps, uint64_t hold_ms, 
                           const char* exit_reason, uint64_t trade_num) {
        if (!csv_initialized_) initShadowCSV();
        if (!shadow_csv_.is_open()) return;
        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000;
        
        // v4.5.0: Include engine_id for attribution
        shadow_csv_ << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
                    << "." << std::setfill('0') << std::setw(3) << now_ms << ","
                    << Chimera::engine_id_str(engine_id) << ","  // Engine ID from parameter
                    << symbol << ","
                    << side << ","
                    << std::fixed << std::setprecision(5) << entry << ","
                    << exit << ","
                    << std::setprecision(2) << pnl_bps << ","
                    << hold_ms << ","
                    << exit_reason << ","
                    << trade_num << "\n";
        shadow_csv_.flush();
    }
};

} // namespace Omega

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
