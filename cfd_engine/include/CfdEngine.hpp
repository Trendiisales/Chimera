// =============================================================================
// CfdEngine.hpp - cTrader FIX Trading Engine
// =============================================================================
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
        snprintf(buf, sizeof(buf), fmt, args...);
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
// CfdEngine - cTrader FIX Trading Engine with MarketState Integration
// =============================================================================
class CfdEngine {
public:
    static constexpr int CPU_CORE = 2;
    
    CfdEngine() 
        : running_(false), connected_(false), kill_switch_(nullptr), execEngine_(centralMicro_)
    {
        DBG_LOG("CfdEngine constructor called");
        forex_symbols_ = {"EURUSD", "GBPUSD", "USDJPY", "AUDUSD"};
        metals_symbols_ = {"XAUUSD", "XAGUSD"};
        indices_symbols_ = {"US30", "US100"};
        
        // Configure market state classifier thresholds
        stateClassifier_.config().vpin_max = 0.60;
        stateClassifier_.config().spread_max_bps = 10.0;
    }
    
    ~CfdEngine() { stop(); }
    
    void setFIXConfig(const FIXConfig& cfg) { fixConfig_ = cfg; }
    void setForexSymbols(const std::vector<std::string>& s) { forex_symbols_ = s; }
    void setMetalsSymbols(const std::vector<std::string>& s) { metals_symbols_ = s; }
    void setIndicesSymbols(const std::vector<std::string>& s) { indices_symbols_ = s; }
    void setKillSwitch(GlobalKillSwitch* ks) { kill_switch_ = ks; }
    void setOrderCallback(std::function<void(const char*, int8_t, double, double, double)> cb) { orderCallback_ = std::move(cb); }
    
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
        
        // Diagnostic logging - v6.73: More frequent + log all block reason changes
        static uint64_t diag_counter = 0;
        static const char* last_reason = "";
        static uint64_t last_reason_time = 0;
        
        bool is_diag_symbol = (strcmp(tick.symbol, "US30") == 0 || 
                               strcmp(tick.symbol, "XAUUSD") == 0 ||
                               strcmp(tick.symbol, "EURUSD") == 0);
        bool should_log_periodic = is_diag_symbol && (++diag_counter % 200 == 0);  // Every 200 ticks instead of 500
        
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
        
        // v6.73: Log EVERY block reason change to debug log
        bool reason_changed = (strcmp(scalp.reason, last_reason) != 0);
        uint64_t now_ms = start_ns / 1000000;
        bool enough_time_passed = (now_ms - last_reason_time) > 1000;  // At least 1 second between logs
        
        if (reason_changed && enough_time_passed) {
            DBG_LOGF("REASON_CHANGE %s: %s -> %s", tick.symbol, last_reason, scalp.reason);
            last_reason = scalp.reason;
            last_reason_time = now_ms;
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
        int conviction = scalp.shouldTrade() ? 7 : 4;
        
        if (marketStateCallback_) {
            marketStateCallback_(mktState, intent, conviction, scalp.reason);
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
        // EXECUTE TRADE
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
                double trade_pnl = scalp.is_exit ? scalp.realized_pnl : 0.0;
                submitOrder(tick.symbol, scalp.direction, scalp.size, (tick.bid + tick.ask) / 2.0, trade_pnl);
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
        
        // Log every 1000th tick to file
        static uint64_t tick_count = 0;
        if ((++tick_count % 1000) == 1) {
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
        if (r.isFill()) stats_.orders_filled.fetch_add(1, std::memory_order_relaxed);
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
        
        // XAUUSD / Gold
        if (sym == "XAUUSD" || sym == "GOLD") {
            meta.min_trade_size = 0.01;
            meta.contract_size = 100.0;
            meta.tick_size = 0.01;
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
    
    inline void submitOrder(const char* symbol, int8_t side, double qty, double price, double pnl = 0.0) {
        // =====================================================================
        // v6.88 EXECUTION PATH DEBUG + SIZE FLOORING
        // =====================================================================
        CfdSymbolMeta meta = getSymbolMeta(symbol);
        double original_size = qty;
        double final_size = qty;
        
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
        
        // LOG EXECUTION CHECK
        std::cout << "[EXEC_CHECK] " << symbol 
                  << " side=" << (side > 0 ? "BUY" : "SELL")
                  << " raw=" << original_size
                  << " final=" << final_size
                  << " min=" << meta.min_trade_size
                  << " price=" << price << "\n";
        
        // RISK CHECK WITH DEBUG
        if (!riskGuard_.checkOrder(final_size, side)) {
            std::cout << "[EXEC_VETO] " << symbol << " RISK_GUARD_BLOCKED"
                      << " event=" << static_cast<int>(riskGuard_.getLastEvent()) << "\n";
            return;
        }
        
        // CALLBACK
        if (orderCallback_) orderCallback_(symbol, side, final_size, price, pnl);
        
        // QUEUE INTENT
        Intent::Side iSide = (side > 0) ? Intent::BUY : Intent::SELL;
        Intent intent(iSide, symbol, final_size, nowNs());
        intentQueue_.push(intent);
        stats_.orders_sent.fetch_add(1, std::memory_order_relaxed);
        
        std::cout << "[EXEC_QUEUED] " << symbol << " " << (side > 0 ? "BUY" : "SELL") 
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
    CTraderFIXClient fixClient_;
    
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
};

} // namespace Omega

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
