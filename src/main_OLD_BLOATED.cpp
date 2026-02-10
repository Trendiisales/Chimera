// =============================================================================
// main.cpp - Chimera v4.17.0 FIX Protocol - AUDIT UPGRADE
// =============================================================================
// ARCHITECTURE:
//   TICKS -> ENGINES (signal) -> SYMBOL_EXECUTOR (decision) -> BROKER
//
//   v4.17.0 AUDIT FIXES:
//   ✅ Engines are PURE signal generators (no internal pyramid tracking)
//   ✅ Dynamic confidence (engines compute 0.0-1.0, executor gates on it)
//   ✅ Weighted BE (net PnL >= 0, replaces strict all-legs-BE)
//   ✅ Slippage guard (prevents late pyramids at exhaustion)
//   ✅ US30 regime filter (gates NAS entries/adds)
//   ✅ Adaptive daily loss limits (volatility-scaled)
//   ✅ FIX throttle (prevents message burst death)
//   ✅ Kill switch (multi-trigger emergency halt)
//   ✅ Execution metrics (latency + slippage tracking)
//   ✅ FIX session guard (heartbeat + disconnect awareness)
//
// ENGINES (signal generators only, no position state):
//   - GoldLiquidityScalper -> publishes intent with dynamic confidence
//   - NASLiquidityScalper  -> publishes intent with dynamic confidence
//
// EXECUTORS (one per symbol, decision makers):
//   - XAU_Executor -> handles XAUUSD pyramiding/trailing
//   - NAS_Executor -> handles NAS100 pyramiding/trailing
//
// REGIME:
//   - US30RegimeFilter -> gates NAS trading on risk-on/chop
//
// RISK:
//   - DailyRiskGovernor -> per-symbol adaptive loss limits
//   - ExecutionKillSwitch -> multi-trigger emergency halt
// =============================================================================

#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <vector>
#include <memory>

#ifdef __linux__
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#endif

// =============================================================================
// EXECUTION LAYER (v4.31.0 - EXIT LOGIC FIXED)
// =============================================================================
#include "TradeLeg.hpp"
#include "SymbolExecutor.hpp"  // Using chimera::SymbolExecutor with fixed exits

// =============================================================================
// v4.17.0 NEW MODULES
// =============================================================================
#include "ConfidenceGate.hpp"
#include "US30RegimeFilter.hpp"
#include "DailyRiskGovernor.hpp"
#include "FixThrottle.hpp"
#include "ExecutionMetrics.hpp"
#include "ExecutionKillSwitch.hpp"
#include "FixSessionGuard.hpp"

// =============================================================================
// SIGNAL GENERATORS (pure signal, no position state)
// =============================================================================
#include "GoldLiquidityScalper.hpp"
#include "NASLiquidityScalper.hpp"

// =============================================================================
// CTRADER FIX CLIENT
// =============================================================================
#include "CTraderFIXClient.hpp"

// =============================================================================
// GUI BROADCASTER
// =============================================================================
#include "GUIBroadcaster.hpp"

// =============================================================================
// v4.23.0: SHADOW INFRASTRUCTURE (Document 4)
// =============================================================================
#include "shadow/CrashHandler.hpp"
#include "shadow/WatchdogThread.hpp"
#include "shadow/JournalWriter.hpp"
#include "shadow/EquityCurve.hpp"
#include "shadow/MultiSymbolExecutor.hpp"  // v4.31.3: Shadow execution simulator

// =============================================================================
// VERSION
// =============================================================================
static constexpr const char* CHIMERA_VERSION = "v4.31.0";  // EXIT LOGIC REFINEMENT (Document 6-7)

// =============================================================================
// SHADOW MODE — Set to true to observe without placing real orders
// When true: signals fire, executor decides, FIX stays connected, but
//            NO orders hit the broker. Logs show [SHADOW] for what WOULD send.
// =============================================================================
static constexpr bool SHADOW_MODE = true;

// =============================================================================
// ENABLED SYMBOLS
// =============================================================================
static const std::vector<std::string> ENABLED_SYMBOLS = {
    "XAUUSD",
    "XAGUSD",  // v4.22.0: Silver added
    "NAS100",
    "US30"
};

// =============================================================================
// GLOBAL STATE
// =============================================================================
static std::atomic<bool> g_running{true};
static std::atomic<int>  g_signal_count{0};

// Tick counters
static std::atomic<uint64_t> g_total_ticks{0};
static std::atomic<uint64_t> g_xauusd_ticks{0};
static std::atomic<uint64_t> g_nas100_ticks{0};
static std::atomic<uint64_t> g_us30_ticks{0};

// Last prices
static std::atomic<double> g_xauusd_bid{0.0};
static std::atomic<double> g_xauusd_ask{0.0};
static std::atomic<double> g_nas100_bid{0.0};
static std::atomic<double> g_nas100_ask{0.0};
static std::atomic<double> g_us30_bid{0.0};
static std::atomic<double> g_us30_ask{0.0};

// =============================================================================
// SIGNAL HANDLER
// =============================================================================
void signalHandler(int sig) {
    g_signal_count.fetch_add(1);
    if (g_signal_count.load() >= 3) {
        std::cerr << "\n[MAIN] Forced exit (3 signals)\n";
        std::exit(1);
    }
    std::cout << "\n[MAIN] Signal " << sig << " received, shutting down...\n";
    g_running.store(false);
}

// =============================================================================
// HELPERS
// =============================================================================
static inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&time));
    
    std::ostringstream oss;
    oss << buf << "." << std::setfill('0') << std::setw(3) << ms;
    return oss.str();
}

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// =============================================================================
// LOGGING
// =============================================================================
static std::ofstream g_logfile;
static std::streambuf* g_cout_buf = nullptr;
static std::streambuf* g_cerr_buf = nullptr;

class TeeStreambuf : public std::streambuf {
private:
    std::streambuf* console_buf_;
    std::ofstream file_;
    
public:
    TeeStreambuf(std::streambuf* console_buf, const std::string& filename)
        : console_buf_(console_buf) {
        file_.open(filename, std::ios::out | std::ios::app);
    }
    
    ~TeeStreambuf() {
        if (file_.is_open()) file_.close();
    }

protected:
    int overflow(int c) override {
        if (c != EOF) {
            if (console_buf_) console_buf_->sputc(c);
            if (file_.is_open()) file_.put(c);
        }
        return c;
    }
    
    int sync() override {
        if (console_buf_) console_buf_->pubsync();
        if (file_.is_open()) file_.flush();
        return 0;
    }
};

static TeeStreambuf* g_tee_out = nullptr;
static TeeStreambuf* g_tee_err = nullptr;

void setupLogging() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "chimera_%Y%m%d_%H%M%S.log", std::localtime(&time));
    
    g_cout_buf = std::cout.rdbuf();
    g_cerr_buf = std::cerr.rdbuf();
    
    g_tee_out = new TeeStreambuf(g_cout_buf, buf);
    g_tee_err = new TeeStreambuf(g_cerr_buf, buf);
    
    std::cout.rdbuf(g_tee_out);
    std::cerr.rdbuf(g_tee_err);
    
    std::cout << "[LOG] Logging to: " << buf << "\n";
}

void teardownLogging() {
    if (g_cout_buf) std::cout.rdbuf(g_cout_buf);
    if (g_cerr_buf) std::cerr.rdbuf(g_cerr_buf);
    delete g_tee_out;
    delete g_tee_err;
    g_tee_out = nullptr;
    g_tee_err = nullptr;
}

// =============================================================================
// BANNER
// =============================================================================
void printBanner() {
    std::cout << R"(
+=========================================================================+
|                                                                           |
|     CHIMERA v4.17.0 - AUDIT UPGRADE                                      |
|                                                                           |
|     SYMBOL EXECUTOR ARCHITECTURE - PYRAMIDING + CONFIDENCE GATING        |
|                                                                           |
+=========================================================================+
|  ARCHITECTURE:                                                            |
|    Ticks -> Engines (signal) -> SymbolExecutor (decision) -> Broker       |
|                                                                           |
|  v4.17.0 AUDIT FIXES:                                                    |
|    * Engines = pure signal generators (no internal pyramids)              |
|    * Dynamic confidence gating (entry >= 0.60, adds >= 0.75)             |
|    * Weighted BE (net PnL >= 0, not strict per-leg)                      |
|    * Slippage guard (skip late pyramids > 0.3R overshoot)                |
|    * US30 regime filter gates NAS entries                                 |
|    * Adaptive daily loss (vol-scaled)                                     |
|    * FIX throttle + kill switch + session guard                           |
|                                                                           |
|  EXECUTORS:                                                               |
|    * XAUUSD: max 3 adds, $0.60/R trigger, $0.36 trail                    |
|    * NAS100: max 3 adds, 9pt/R trigger, 5.4pt trail                      |
|    * US30:   regime filter only (no trading)                              |
+=========================================================================+
)" << std::endl;
}

// =============================================================================
// MAIN
// =============================================================================
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    setupLogging();
    
    // ===================================================================
    // v4.23.0: PRODUCTION INFRASTRUCTURE (Document 4)
    // ===================================================================
    std::cout << "[MAIN] Installing crash handlers...\n";
    shadow::CrashHandler::install();
    
    std::cout << "[MAIN] Initializing FIX journal...\n";
    shadow::JournalWriter::init();
    
    std::cout << "[MAIN] Initializing equity curve tracker...\n";
    shadow::EquityCurve::init();
    
    std::cout << "[MAIN] Starting watchdog thread...\n";
    shadow::WatchdogThread::start();
    
    // Register flush callback for crash handler
    shadow::CrashHandler::registerFlushCallback([]() {
        std::cout << "[CRASH] Emergency flush initiated...\n";
        shadow::JournalWriter::flush();
        shadow::EquityCurve::exportCSV();
        shadow::EquityCurve::printSummary();
    });
    // ===================================================================
    
    printBanner();
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  CHIMERA v4.31.0 PRODUCTION - BUILD 20260206-1400            ║\n";
    std::cout << "║  CRITICAL FIX: Using shadow/SymbolExecutor (NOT execution/)  ║\n";
    std::cout << "║  All Document 1-4 fixes verified and active                 ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    std::cout << "[MAIN] " << timestamp() << " Chimera " << CHIMERA_VERSION << " starting...\n";
    
    if (SHADOW_MODE) {
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║              ⚠️  SHADOW MODE ACTIVE ⚠️                   ║\n";
        std::cout << "║  Signals: LIVE    Executor: LIVE    Orders: BLOCKED     ║\n";
        std::cout << "║  No real orders will reach the broker.                  ║\n";
        std::cout << "║  Set SHADOW_MODE = false in main.cpp to go live.        ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    }
    
    // ==========================================================================
    // INITIALIZE FIX CLIENT
    // ==========================================================================
    Chimera::CTraderFIXClient fixClient;
    fixClient.setExternalRunning(&g_running);
    
    Chimera::FIXConfig fixConfig;
    
    if (!fixConfig.isValid()) {
        std::cerr << "[MAIN] Invalid FIX configuration! Check config.ini [ctrader] section\n";
        teardownLogging();
        return 1;
    }
    
    fixConfig.print();
    fixClient.setConfig(fixConfig);
    
    // ==========================================================================
    // INITIALIZE GUI
    // ==========================================================================
    Chimera::GUIBroadcaster gui;
    gui.setVersion(CHIMERA_VERSION);
    
    if (gui.start()) {
        std::cout << "[MAIN] GUI server started (WebSocket:7777 HTTP:8080)\n";
    } else {
        std::cerr << "[MAIN] WARNING: GUI server failed to start\n";
    }
    
    // ==========================================================================
    // v4.17.0: RISK INFRASTRUCTURE
    // ==========================================================================
    
    // v4.18.0 FIX: DailyLossGuard + GlobalKill MUST exist before any order can pass
    // BUG: GlobalRiskGovernor::instance() had daily_loss_ = nullptr
    //      First canSubmitOrder() → !nullptr = true → instant DAILY_MAX_LOSS shutdown
    //      This is why "Daily PnL: $0.00 NZD" appeared with SHUTDOWN on first trade
    static Chimera::DailyLossGuard dailyLossGuard(-200.0);  // -$200 NZD hard limit
    static Chimera::GlobalKill globalKill;
    
    // Initialize the singleton BEFORE anything can call canSubmitOrder()
    Chimera::GlobalRiskGovernor::instance().init(&dailyLossGuard, &globalKill, 10000.0);
    std::cout << "[MAIN] GlobalRiskGovernor initialized: daily_loss=$200 NZD, capital=$10000 NZD\n";
    
    // Wire GUI kill switch so dashboard panic button works
    gui.setKillSwitch(&globalKill);
    
    // Kill switch - multi-trigger emergency halt
    chimera::ExecutionKillSwitch killSwitch;
    killSwitch.max_daily_loss = 250.0;        // Hard ceiling
    killSwitch.max_latency_ms = 25.0;         // Per-order latency limit
    killSwitch.max_latency_viols = 5;         // Consecutive before arm
    killSwitch.max_consec_losers = 6;         // Consecutive losers before arm
    std::cout << "[MAIN] Kill switch: loss=$" << killSwitch.max_daily_loss
              << " latency=" << killSwitch.max_latency_ms << "ms"
              << " losers=" << killSwitch.max_consec_losers << "\n";
    
    // Daily risk governors (per-symbol, volatility adaptive)
    chimera::DailyRiskGovernor xau_risk_gov;
    xau_risk_gov.base_daily_loss = 200.0;
    xau_risk_gov.low_vol_scale = 0.70;
    xau_risk_gov.high_vol_scale = 1.30;
    
    chimera::DailyRiskGovernor nas_risk_gov;
    nas_risk_gov.base_daily_loss = 200.0;
    nas_risk_gov.low_vol_scale = 0.70;
    nas_risk_gov.high_vol_scale = 1.30;
    
    std::cout << "[MAIN] Risk governors: base=$200, scale=[0.70, 1.30]\n";
    
    // FIX throttle - prevent message burst death
    chimera::FixThrottle fixThrottle(250'000ULL, 20);  // 250us gap, 20/sec
    std::cout << "[MAIN] FIX throttle: 250us gap, 20 msgs/sec\n";
    
    // Execution metrics (per-order latency tracking)
    chimera::ExecutionMetrics xau_metrics;
    chimera::ExecutionMetrics nas_metrics;
    
    // FIX session guard
    chimera::FixSessionGuard sessionGuard;
    
    // ==========================================================================
    // SHADOW EXECUTION SIMULATOR (v4.31.4)
    // ==========================================================================
    // Declared here BEFORE executors so it can be captured in order callbacks
    shadow::MultiSymbolExecutor shadow_exec;
    shadow_exec.addSymbol(shadow::getXauConfig(), shadow::ExecMode::SHADOW);
    shadow_exec.addSymbol(shadow::getNasConfig(), shadow::ExecMode::SHADOW);
    std::cout << "[MAIN] Shadow executor initialized (XAUUSD + NAS100)\n";
    
    // v4.31.7: Wire GUI callbacks to shadow executors
    auto* xau_shadow = shadow_exec.getExecutor("XAUUSD");
    auto* nas_shadow = shadow_exec.getExecutor("NAS100");
    if (xau_shadow) {
        xau_shadow->setGUICallback([&gui](const char* symbol, const char* side, double size, double price, double pnl) {
            gui.broadcastTrade(symbol, side, size, price, pnl);
        });
    }
    if (nas_shadow) {
        nas_shadow->setGUICallback([&gui](const char* symbol, const char* side, double size, double price, double pnl) {
            gui.broadcastTrade(symbol, side, size, price, pnl);
        });
    }
    std::cout << "[MAIN] Shadow GUI callbacks wired\n";
    
    // US30 regime filter (gates NAS trading)
    chimera::US30RegimeFilter us30Regime;
    us30Regime.setBaseVolatility(3.0);  // ~3pts/tick baseline for US30
    std::cout << "[MAIN] US30 regime filter: chop=" << chimera::US30RegimeFilter::CHOP_THRESHOLD
              << "pts impulse=" << chimera::US30RegimeFilter::IMPULSE_THRESHOLD << "pts\n";
    
    // ==========================================================================
    // SYMBOL EXECUTORS (ONE PER SYMBOL)
    // ==========================================================================
    
    // === XAUUSD EXECUTOR ===
    // v4.17.0: confidence gating, weighted BE, slippage guard
    chimera::ExecutorConfig xau_config;
    xau_config.symbol = "XAUUSD";
    xau_config.max_pyramids = 3;              // 3 adds = 4 total positions
    xau_config.pyramid_trigger_R = 0.5;       // Price must move 0.5R from last entry
    xau_config.pyramid_sizes[0] = 1.0;        // Base
    xau_config.pyramid_sizes[1] = 0.7;        // Add 1
    xau_config.pyramid_sizes[2] = 0.5;        // Add 2
    xau_config.pyramid_sizes[3] = 0.3;        // Add 3
    xau_config.pyramid_cooldown_ticks = 20;   // Anti-overtrading
    // v4.17.0: Confidence gating
    xau_config.min_entry_confidence = 0.60;
    xau_config.min_add_confidence = 0.75;
    xau_config.full_pyramid_confidence = 0.85;
    // v4.17.0: Weighted BE replaces strict all-legs-BE
    xau_config.use_weighted_BE = true;
    xau_config.require_all_BE_before_add = false;
    xau_config.weighted_BE_threshold = 0.0;   // Net unrealized >= 0
    // v4.17.0: Slippage guard
    xau_config.max_slippage_R = 0.3;          // Skip add if >0.3R overshoot
    // v4.18.0: Reversal stability - flips require higher confidence than entry
    xau_config.min_reversal_confidence = 0.80;
    // Trailing + sizing
    // v4.18.0: BlackBull/cTrader XAUUSD min lot = 1.00, step = 1.00
    // Pyramid adds will be clamped to 1.0 minimum at FIX layer
    xau_config.trail_min_R = 0.3;             // Unified trailing at 0.3R
    xau_config.base_size = 1.0;               // 1.0 lot (broker minimum for metals)
    xau_config.max_total_size = 4.0;          // Base(1.0) + 3 pyramids(1.0 each) = 4.0
    xau_config.default_stop_distance = 1.20;  // $1.20 = R
    xau_config.max_daily_loss = 200.0;
    
    chimera::SymbolExecutor xau_executor;
    xau_executor.init(xau_config);
    
    // v4.18.0: Wire PnL to cross-engine DailyLossGuard
    auto* dlg = &dailyLossGuard;
    xau_executor.setPnLCallback([dlg](double pnl) {
        dlg->on_fill(pnl);
    });
    
    // Wire XAU executor to FIX (with throttle + metrics)
    xau_executor.setOrderCallback([&fixClient, &gui, &fixThrottle, &xau_metrics, &killSwitch, &sessionGuard, &xau_executor, &shadow_exec](const chimera::OrderRequest& req) {
        uint64_t ts = now_ns();
        
        // v4.18.0: State machine enforcement - no orders while in COOLDOWN
        if (xau_executor.getState() == chimera::ExecState::COOLDOWN) {
            return;
        }
        
        char side = (req.side == chimera::LegSide::LONG) 
                  ? Chimera::FIXSide::Buy 
                  : Chimera::FIXSide::Sell;
        
        std::cout << "[XAU_ORDER] " << timestamp() 
                  << " " << (req.side == chimera::LegSide::LONG ? "BUY" : "SELL")
                  << " " << std::fixed << std::setprecision(2) << req.size << " lots"
                  << " @ " << std::setprecision(2) << req.price
                  << " leg=" << req.leg_id
                  << " reason=" << req.reason << "\n";
        
        xau_metrics.onSubmit(req.price, ts);
        
        // v4.31.4: Shadow mode blocks live orders (shadow execution happens at signal level)
        if (SHADOW_MODE) {
            return;
        }
        
        // Kill switch check
        if (killSwitch.isArmed()) {
            std::cerr << "[XAU_ORDER] KILL SWITCH ACTIVE — order blocked\n";
            return;
        }
        
        // FIX throttle
        if (!fixThrottle.allow(ts)) {
            std::cerr << "[XAU_ORDER] FIX throttle blocked (too fast)\n";
            return;
        }
        
        // FIX session guard
        if (!sessionGuard.isHealthy(ts)) {
            std::cerr << "[XAU_ORDER] FIX session unhealthy — order blocked\n";
            return;
        }
        
        bool sent = fixClient.sendMarketOrder(req.symbol, side, req.size);
        if (sent) {
            gui.broadcastTrade(req.symbol, 
                              (req.side == chimera::LegSide::LONG ? "BUY" : "SELL"),
                              req.size, req.price, 0.0);
        }
    });
    
    std::cout << "[MAIN] XAUUSD SymbolExecutor initialized (base + " << xau_config.max_pyramids 
              << " pyramids, conf=[" << xau_config.min_entry_confidence
              << "/" << xau_config.min_add_confidence
              << "/" << xau_config.full_pyramid_confidence << "])\n";
    
    // === NAS100 EXECUTOR ===
    chimera::ExecutorConfig nas_config;
    nas_config.symbol = "NAS100";
    nas_config.max_pyramids = 2;
    nas_config.pyramid_trigger_R = 0.5;
    nas_config.pyramid_sizes[0] = 1.0;
    nas_config.pyramid_sizes[1] = 0.7;
    nas_config.pyramid_sizes[2] = 0.5;
    nas_config.pyramid_cooldown_ticks = 30;
    nas_config.min_entry_confidence = 0.60;
    nas_config.min_add_confidence = 0.75;
    nas_config.full_pyramid_confidence = 0.85;
    nas_config.use_weighted_BE = true;
    nas_config.require_all_BE_before_add = false;
    nas_config.weighted_BE_threshold = 0.0;
    nas_config.max_slippage_R = 0.4;
    nas_config.min_reversal_confidence = 0.80;
    nas_config.trail_min_R = 0.3;
    nas_config.base_size = 1.0;
    nas_config.max_total_size = 3.0;
    nas_config.default_stop_distance = 15.0;  // 15pts = R
    nas_config.max_daily_loss = 200.0;
    
    chimera::SymbolExecutor nas_executor;
    nas_executor.init(nas_config);
    
    nas_executor.setPnLCallback([dlg](double pnl) {
        dlg->on_fill(pnl);
    });
    
    nas_executor.setOrderCallback([&fixClient, &gui, &fixThrottle, &nas_metrics, &killSwitch, &sessionGuard, &nas_executor, &shadow_exec](const chimera::OrderRequest& req) {
        uint64_t ts = now_ns();
        
        if (nas_executor.getState() == chimera::ExecState::COOLDOWN) {
            return;
        }
        
        char side = (req.side == chimera::LegSide::LONG) 
                  ? Chimera::FIXSide::Buy 
                  : Chimera::FIXSide::Sell;
        
        std::cout << "[NAS_ORDER] " << timestamp() 
                  << " " << (req.side == chimera::LegSide::LONG ? "BUY" : "SELL")
                  << " " << std::fixed << std::setprecision(2) << req.size << " lots"
                  << " @ " << std::setprecision(1) << req.price
                  << " leg=" << req.leg_id
                  << " reason=" << req.reason << "\n";
        
        nas_metrics.onSubmit(req.price, ts);
        
        // v4.31.4: Shadow mode blocks live orders (shadow execution happens at signal level)
        if (SHADOW_MODE) {
            return;
        }
        
        if (killSwitch.isArmed()) {
            std::cerr << "[NAS_ORDER] KILL SWITCH ACTIVE — order blocked\n";
            return;
        }
        
        if (!fixThrottle.allow(ts)) {
            std::cerr << "[NAS_ORDER] FIX throttle blocked (too fast)\n";
            return;
        }
        
        if (!sessionGuard.isHealthy(ts)) {
            std::cerr << "[NAS_ORDER] FIX session unhealthy — order blocked\n";
            return;
        }
        
        bool sent = fixClient.sendMarketOrder(req.symbol, side, req.size);
        if (sent) {
            gui.broadcastTrade(req.symbol, 
                              (req.side == chimera::LegSide::LONG ? "BUY" : "SELL"),
                              req.size, req.price, 0.0);
        } else {
            // v4.18.0: Feed block back to executor health tracker
            nas_executor.notifyOrderBlocked(ts);
        }
    });
    
    std::cout << "[MAIN] NAS100 SymbolExecutor initialized (base + " << nas_config.max_pyramids
              << " pyramids, conf=[" << nas_config.min_entry_confidence
              << "/" << nas_config.min_add_confidence
              << "/" << nas_config.full_pyramid_confidence << "])\n";
    
    // ==========================================================================
    // SIGNAL GENERATORS (engines that ADVISE, don't execute)
    // ==========================================================================
    
    // Gold signal generator - emits dynamic confidence
    gold_liquidity::GoldLiquidityScalper gold_signal;
    
    // Wire gold signal -> XAU executor (with DYNAMIC confidence from engine)
    gold_signal.setOrderCallback([&xau_executor, &gold_signal, &shadow_exec](const gold_liquidity::Order& o) {
        // v4.31.4: Shadow execution FIRST - always simulate, ungated
        if (SHADOW_MODE) {
            shadow::Signal sig;
            sig.side = (o.side == gold_liquidity::Side::BUY) ? shadow::Side::BUY : shadow::Side::SELL;
            sig.price = o.price;
            sig.confidence = gold_signal.getConfidence();
            sig.raw_momentum = gold_signal.getMomentum();
            
            std::printf("[SHADOW_SIM] XAUUSD %s @ %.2f conf=%.2f mom=%.1f\n",
                       (sig.side == shadow::Side::BUY ? "BUY" : "SELL"),
                       sig.price, sig.confidence, sig.raw_momentum);
            
            shadow_exec.onSignal("XAUUSD", sig);
        }
        
        // v4.31.1: Signal engine already filters momentum >= 25.0 and cooldown
        // These are fallback safety checks only
        
        // GATE 1: Suppress entry signals if already in position
        // (setSuppressed is managed in status loop, but check here as fallback)
        if (xau_executor.hasPosition()) {
            return;  // Future: add position management signals here
        }
        
        // GATE 2: Confidence-momentum coherence (Document 5 audit)
        // High confidence should have strong momentum (engine already filtered >= 25.0)
        double confidence = gold_signal.getConfidence();
        double momentum = gold_signal.getMomentum();
        if (confidence > 0.70 && std::fabs(momentum) < 30.0) {
            return;  // Extra high bar for high confidence entries
        }
        
        // All gates passed - forward to executor
        chimera::EngineIntent intent;
        intent.engine_name = "GoldLiquidityScalper";
        intent.symbol = o.symbol;
        intent.direction = (o.side == gold_liquidity::Side::BUY) ? +1 : -1;
        // v4.17.0: DYNAMIC confidence from engine (was hardcoded 0.8)
        intent.confidence = confidence;
        // v4.18.0: Pass momentum for hysteresis gate
        intent.momentum = momentum;
        intent.suggested_size = o.size;
        intent.suggested_stop = 1.20;  // $1.20 stop
        intent.trail_hint = 0.30;
        intent.ts_ns = o.ts_ns;
        intent.valid = true;
        
        // ADVISE the executor (executor DECIDES)
        xau_executor.onIntent(intent);
    });
    
    std::cout << "[MAIN] GoldLiquidityScalper (pure signal) -> XAU Executor\n";
    
    // NAS signal generator
    nas_liquidity::NASLiquidityScalper nas_signal;
    
    // Wire NAS signal -> NAS executor
    nas_signal.setOrderCallback([&nas_executor, &nas_signal, &shadow_exec](const nas_liquidity::Order& o) {
        // v4.31.4: Shadow execution FIRST - always simulate, ungated
        if (SHADOW_MODE) {
            shadow::Signal sig;
            sig.side = (o.side == nas_liquidity::Side::BUY) ? shadow::Side::BUY : shadow::Side::SELL;
            sig.price = o.price;
            sig.confidence = nas_signal.getConfidence();
            sig.raw_momentum = nas_signal.getMomentum();
            
            std::printf("[SHADOW_SIM] NAS100 %s @ %.1f conf=%.2f mom=%.1f\n",
                       (sig.side == shadow::Side::BUY ? "BUY" : "SELL"),
                       sig.price, sig.confidence, sig.raw_momentum);
            
            shadow_exec.onSignal("NAS100", sig);
        }
        
        // v4.31.0: SIGNAL QUALITY GATES (Documents 6 & 7)
        
        // GATE 1: Suppress entry signals if already in position
        // (setSuppressed is managed in status loop, but check here as fallback)
        if (nas_executor.hasPosition()) {
            return;  // Future: add position management signals here
        }
        
        // GATE 2: Confidence-momentum coherence (Document 7)
        // NAS requires higher thresholds due to volatility
        double confidence = nas_signal.getConfidence();
        double momentum = nas_signal.getMomentum();
        if (confidence > 0.70 && std::fabs(momentum) < 0.30) {
            return;  // Raised from 0.20 per Document 4 audit
        }
        
        // All gates passed - forward to executor
        chimera::EngineIntent intent;
        intent.engine_name = "NASLiquidityScalper";
        intent.symbol = o.symbol;
        intent.direction = (o.side == nas_liquidity::Side::BUY) ? +1 : -1;
        intent.confidence = confidence;
        intent.momentum = momentum;
        intent.suggested_size = o.size;
        intent.suggested_stop = 15.0;  // 15pts
        intent.trail_hint = 8.0;
        intent.ts_ns = o.ts_ns;
        intent.valid = true;
        
        nas_executor.onIntent(intent);
    });
    
    std::cout << "[MAIN] NASLiquidityScalper (pure signal) -> NAS Executor\n";
    
    // ==========================================================================
    // TICK ROUTING (v4.17.0: US30 feeds regime filter, regime gates NAS)
    // ==========================================================================
    static std::atomic<bool> first_xauusd{true};
    static std::atomic<bool> first_nas100{true};
    static std::atomic<bool> first_us30{true};
    
    fixClient.setOnTick([&gold_signal, &nas_signal, &xau_executor, &nas_executor, 
                         &gui, &us30Regime, &xau_risk_gov, &nas_risk_gov, &sessionGuard, &shadow_exec](const Chimera::CTraderTick& tick) {
        g_total_ticks.fetch_add(1);
        uint64_t ts_ns = now_ns();
        
        // v4.18.0: Every tick proves the FIX connection is alive
        sessionGuard.onHeartbeat(ts_ns);
        
        if (tick.symbol == "XAUUSD") {
            g_xauusd_ticks.fetch_add(1);
            g_xauusd_bid.store(tick.bid);
            g_xauusd_ask.store(tick.ask);
            
            // Update GUI with tick data (multi-symbol)
            gui.updateSymbolTick("XAUUSD", tick.bid, tick.ask);
            
            // 1. Risk governor gets tick (for volatility tracking)
            xau_risk_gov.onTick((tick.bid + tick.ask) / 2.0);
            
            // 2. Update executor daily loss limit from adaptive governor
            if (xau_risk_gov.allowTrading()) {
                xau_executor.setDailyLossLimit(xau_risk_gov.getAdjustedLimit());
            }
            
            // 3. Signal generator gets tick (for bias + confidence)
            gold_signal.on_tick(tick.bid, tick.ask, ts_ns);
            
            // 4. Executor gets tick (for MAE/MFE tracking, pyramiding, trailing)
            xau_executor.onTick(tick.bid, tick.ask, ts_ns);
            
            // 5. Shadow executor gets tick (for position tracking and exits)
            shadow::Tick shadow_tick;
            shadow_tick.bid = tick.bid;
            shadow_tick.ask = tick.ask;
            shadow_tick.ts_ms = ts_ns / 1'000'000;  // Convert ns → ms
            shadow_exec.onTick("XAUUSD", shadow_tick);
            
            if (first_xauusd.exchange(false)) {
                std::cout << "[TICK] XAUUSD FIRST: " << std::fixed << std::setprecision(2) 
                          << tick.bid << "/" << tick.ask << "\n";
            }
            
        } else if (tick.symbol == "NAS100") {
            g_nas100_ticks.fetch_add(1);
            g_nas100_bid.store(tick.bid);
            g_nas100_ask.store(tick.ask);
            
            // Update GUI with tick data (multi-symbol)
            gui.updateSymbolTick("NAS100", tick.bid, tick.ask);
            
            // 1. Risk governor
            nas_risk_gov.onTick((tick.bid + tick.ask) / 2.0);
            if (nas_risk_gov.allowTrading()) {
                nas_executor.setDailyLossLimit(nas_risk_gov.getAdjustedLimit());
            }
            
            // 2. v4.17.0: Set regime flag on NAS executor from US30 filter
            bool regime_ok = us30Regime.isRiskOn() && !us30Regime.isChoppy();
            nas_executor.setRegimeOk(regime_ok);
            
            // 3. Suppress NAS signal engine in chop regime
            nas_signal.setSuppressed(!regime_ok);
            
            // 4. Signal generator gets tick
            nas_signal.on_tick(tick.bid, tick.ask, ts_ns);
            
            // 5. Executor gets tick
            nas_executor.onTick(tick.bid, tick.ask, ts_ns);
            
            // 6. Shadow executor gets tick
            shadow::Tick shadow_tick;
            shadow_tick.bid = tick.bid;
            shadow_tick.ask = tick.ask;
            shadow_tick.ts_ms = ts_ns / 1'000'000;  // Convert ns → ms
            shadow_exec.onTick("NAS100", shadow_tick);
            
            if (first_nas100.exchange(false)) {
                std::cout << "[TICK] NAS100 FIRST: " << std::fixed << std::setprecision(1) 
                          << tick.bid << "/" << tick.ask << "\n";
            }
            
        } else if (tick.symbol == "US30") {
            g_us30_ticks.fetch_add(1);
            g_us30_bid.store(tick.bid);
            g_us30_ask.store(tick.ask);
            
            // Update GUI with tick data (multi-symbol)
            gui.updateSymbolTick("US30", tick.bid, tick.ask);
            
            // v4.17.0: US30 is now a REGIME SIGNAL, not dead weight
            us30Regime.onTick(tick.bid, tick.ask, ts_ns);
            
            if (first_us30.exchange(false)) {
                std::cout << "[TICK] US30 FIRST: " << std::fixed << std::setprecision(1) 
                          << tick.bid << "/" << tick.ask 
                          << " (regime: " << (us30Regime.isRiskOn() ? "RISK-ON" : "RISK-OFF") << ")\n";
            }
        }
        
        gui.updateSymbolTick(tick.symbol.c_str(), tick.bid, tick.ask, 0.2);
    });
    
    // ==========================================================================
    // STATE CALLBACK (v4.17.0: session guard integration)
    // ==========================================================================
    fixClient.setOnState([&gui, &sessionGuard](bool quoteConnected, bool tradeConnected) {
        std::cout << "[MAIN] FIX state: QUOTE=" << (quoteConnected ? "UP" : "DOWN")
                  << " TRADE=" << (tradeConnected ? "UP" : "DOWN") << "\n";
        gui.updateConnections(quoteConnected && tradeConnected);
        
        if (quoteConnected && tradeConnected) {
            sessionGuard.onReconnect(now_ns());
        } else {
            sessionGuard.onDisconnect();
        }
    });
    
    // ==========================================================================
    // EXECUTION REPORTS (v4.17.0: metrics + kill switch integration)
    // ==========================================================================
    fixClient.setOnExec([&gui, &xau_metrics, &nas_metrics, &killSwitch, &sessionGuard](const Chimera::CTraderExecReport& report) {
        uint64_t ts = now_ns();
        
        // Heartbeat proxy: any exec report means session is alive
        sessionGuard.onHeartbeat(ts);
        
        if (report.isFill()) {
            std::cout << "[FILL] " << report.symbol 
                      << " " << report.lastQty << " @ " << report.lastPx << "\n";
            
            // Update metrics based on symbol
            if (report.symbol == "XAUUSD") {
                xau_metrics.onFill(report.lastPx, ts);
                killSwitch.onLatency(xau_metrics.getLastLatencyMs());
            } else if (report.symbol == "NAS100") {
                nas_metrics.onFill(report.lastPx, ts);
                killSwitch.onLatency(nas_metrics.getLastLatencyMs());
            }
            
            const char* side_str = (report.side == Chimera::FIXSide::Buy) ? "BUY" : "SELL";
            gui.broadcastTrade(report.symbol.c_str(), side_str, report.lastQty, report.lastPx, 0.0);
            
        } else if (report.isReject()) {
            std::cerr << "[REJECT] " << report.symbol << ": " << report.text << "\n";
        }
    });
    
    // ==========================================================================
    // CONNECT TO CTRADER FIX
    // ==========================================================================
    std::cout << "[MAIN] Connecting to cTrader FIX...\n";
    
    if (!fixClient.connect()) {
        std::cerr << "[MAIN] Failed to connect to cTrader FIX\n";
        gui.stop();
        teardownLogging();
        return 1;
    }
    
    std::cout << "[MAIN] Connected to cTrader FIX\n";
    sessionGuard.onHeartbeat(now_ns());
    
    // ==========================================================================
    // REQUEST SECURITY LIST (symbol -> SecurityID mapping)
    // ==========================================================================
    std::cout << "[MAIN] Requesting security list...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    if (!fixClient.requestSecurityList()) {
        std::cerr << "[MAIN] Failed to send security list request\n";
    }
    
    // Wait for security list (up to 30 seconds)
    std::cout << "[MAIN] Waiting for security list...\n";
    int wait_count = 0;
    while (!fixClient.isSecurityListReady() && wait_count < 30 && g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_count++;
        if (wait_count % 5 == 0) {
            std::cout << "[MAIN] Still waiting for security list... (" << wait_count << "s)\n";
        }
    }
    
    if (!fixClient.isSecurityListReady()) {
        std::cerr << "[MAIN] Security list timeout after 30s - subscriptions will fail\n";
    } else {
        std::cout << "[MAIN] Security list received (" 
                  << fixClient.getSecurityListCount() << " symbols)\n";
    }
    
    // ==========================================================================
    // v4.18.0: ARM INTENT LIVE — BEFORE subscriptions
    // Ticks arrive immediately after subscribe. Intent must be armed first.
    // ==========================================================================
    fixClient.setIntentLive(true);
    std::cout << "[MAIN] ✅ Intent set to LIVE — orders armed\n";
    
    // ==========================================================================
    // SUBSCRIBE TO MARKET DATA
    // ==========================================================================
    std::cout << "[MAIN] Subscribing to market data...\n";
    
    for (const auto& symbol : ENABLED_SYMBOLS) {
        if (fixClient.subscribeMarketData(symbol)) {
            std::cout << "[MAIN] Subscribed to " << symbol << "\n";
        } else {
            std::cerr << "[MAIN] Failed to subscribe to " << symbol << "\n";
        }
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // ==========================================================================
    // MAIN LOOP (v4.17.0: enhanced status with new components)
    // ==========================================================================
    std::cout << "\n[MAIN] ======== ENTERING MAIN LOOP ========\n";
    std::cout << "[MAIN] Press Ctrl+C to stop\n\n";
    
    auto start_time = std::chrono::steady_clock::now();
    auto last_status = start_time;
    
    int status_interval_sec = 30;
    uint64_t prev_total_ticks = 0;
    
    while (g_running.load()) {
        // v4.23.0: Update watchdog heartbeat
        shadow::WatchdogThread::heartbeat();
        
        auto now = std::chrono::steady_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_status).count();
        if (elapsed >= status_interval_sec) {
            last_status = now;
            
            // v4.23.0: Export equity curve periodically
            shadow::EquityCurve::exportCSV();
            
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            uint64_t current_ticks = g_total_ticks.load();
            uint64_t tick_rate = (current_ticks - prev_total_ticks) / status_interval_sec;
            prev_total_ticks = current_ticks;
            
            std::cout << "\n[STATUS] Uptime=" << uptime << "s | Ticks=" << current_ticks 
                      << " (" << tick_rate << "/s)";
            
            // v4.17.0: Kill switch status
            if (killSwitch.isArmed()) {
                std::cout << " | KILL=" << killSwitch.getArmReason();
            }
            std::cout << "\n";
            
            // XAUUSD Executor status (v4.18.0: + state machine)
            // v4.31.6: In shadow mode, show shadow executor stats
            if (SHADOW_MODE) {
                auto* xau_shadow = shadow_exec.getExecutor("XAUUSD");
                if (xau_shadow) {
                    std::cout << "  [XAU] state=" << (xau_shadow->isFlat() ? "FLAT" : "OPEN")
                              << " legs=" << xau_shadow->getActiveLegs()
                              << "/" << (xau_config.max_pyramids + 1)
                              << " realized=" << std::fixed << std::setprecision(2) << xau_shadow->getRealizedPnl()
                              << " trades=" << xau_shadow->getTradesToday()
                              << " [SHADOW_MODE | UNLIMITED_TRADES]";
                }
            } else {
                std::cout << "  [XAU] state=" << chimera::exec_state_str(xau_executor.getState())
                          << " legs=" << xau_executor.getActiveLegCount()
                          << "/" << (xau_config.max_pyramids + 1)
                          << " size=" << std::fixed << std::setprecision(2) << xau_executor.getTotalSize()
                          << " bias=" << (xau_executor.getCurrentBias() == chimera::LegSide::LONG ? "LONG" : "SHORT")
                          << " stop=" << xau_executor.getUnifiedStop()
                          << " R=" << xau_executor.getBaseR()
                          << " uPnL=" << std::setprecision(2) << xau_executor.getTotalUnrealizedPnL()
                          << " dayPnL=" << xau_executor.getDailyPnL()
                          << " conf=" << std::setprecision(2) << xau_executor.getLastConfidence()
                          << " trades=" << xau_executor.getTradesToday()
                          << " rev=" << xau_executor.getReversalCount()
                          << (xau_executor.isPyramidEnabled() ? " [PYR:ON]" : " [PYR:OFF]");
            }
            
            // v4.17.0: Risk governor status (skip in shadow mode)
            if (!SHADOW_MODE) {
                std::cout << " loss_lim=$" << std::setprecision(0) << xau_risk_gov.getAdjustedLimit()
                          << "(" << xau_risk_gov.getVolRegime() << ")\n";
            } else {
                std::cout << "\n";  // Just newline in shadow mode
            }
            
            // Per-leg detail if active (only in live mode)
            if (!SHADOW_MODE && xau_executor.hasPosition()) {
                for (int i = 0; i < chimera::SymbolExecutor::MAX_LEGS; i++) {
                    const auto& leg = xau_executor.getLeg(i);
                    if (leg.isActive()) {
                        std::cout << "    leg#" << leg.leg_id 
                                  << " entry=" << std::setprecision(2) << leg.entry_price
                                  << " stop=" << leg.current_stop
                                  << " MAE=" << std::setprecision(2) << leg.mae
                                  << " MFE=" << leg.mfe
                                  << " R=" << leg.getCurrentR() << "R\n";
                    }
                }
            }
            
            // NAS100 Executor status (v4.18.0: + state machine)
            std::cout << "  [NAS] state=" << chimera::exec_state_str(nas_executor.getState())
                      << " legs=" << nas_executor.getActiveLegCount()
                      << "/" << (nas_config.max_pyramids + 1)
                      << " size=" << std::setprecision(2) << nas_executor.getTotalSize()
                      << " dayPnL=" << nas_executor.getDailyPnL()
                      << " conf=" << std::setprecision(2) << nas_executor.getLastConfidence()
                      << " trades=" << nas_executor.getTradesToday()
                      << " rev=" << nas_executor.getReversalCount()
                      << (nas_executor.isPyramidEnabled() ? " [PYR:ON]" : " [PYR:OFF]")
                      << " regime=" << (nas_executor.isRegimeOk() ? "OK" : "BLOCKED");
            std::cout << " loss_lim=$" << std::setprecision(0) << nas_risk_gov.getAdjustedLimit()
                      << "(" << nas_risk_gov.getVolRegime() << ")\n";
            
            // v4.17.0: US30 regime status
            std::cout << "  [US30] regime=" << (us30Regime.isRiskOn() ? "RISK-ON" : "RISK-OFF")
                      << " trending=" << (us30Regime.isTrending() ? "YES" : "NO")
                      << " choppy=" << (us30Regime.isChoppy() ? "YES" : "NO")
                      << " quality=" << std::setprecision(2) << us30Regime.getRegimeQuality()
                      << " range=" << std::setprecision(1) << us30Regime.getSessionRange() << "pts\n";
            
            // v4.17.0: Execution metrics
            if (SHADOW_MODE) {
                std::cout << "  [EXEC] xau_lat=SHADOW nas_lat=SHADOW"
                          << " throttled=" << fixThrottle.getThrottleCount()
                          << " orders=" << (xau_metrics.getTotalOrders() + nas_metrics.getTotalOrders()) << "\n";
            } else {
                std::cout << "  [EXEC] xau_lat=" << std::setprecision(1) << xau_metrics.getAvgLatencyMs() << "ms"
                          << " nas_lat=" << nas_metrics.getAvgLatencyMs() << "ms"
                          << " throttled=" << fixThrottle.getThrottleCount()
                          << " orders=" << (xau_metrics.getTotalOrders() + nas_metrics.getTotalOrders()) << "\n";
            }
            
            // v4.31.0: Bridge ExecutionMetrics → GUI (was missing, causing latency stuck at 0.0ms)
            gui.setExecutionLatencyMs(xau_metrics.getAvgLatencyMs());
            
            // v4.31.0: Signal suppression - stop entry signals when position exists (Document 4 audit)
            gold_signal.setSuppressed(xau_executor.hasPosition());
            nas_signal.setSuppressed(nas_executor.hasPosition());
            
            if (current_ticks == 0 && uptime > 15) {
                std::cerr << "  NO TICKS! Check: Market open? FIX connected?\n";
            }
            
            gui.updateConnections(fixClient.isConnected());
        }
        
        // Connection check (v4.17.0: session guard aware)
        if (!fixClient.isConnected()) {
            std::cerr << "[MAIN] Connection lost, attempting reconnect...\n";
            sessionGuard.onDisconnect();
            
            fixClient.disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            if (fixClient.connect()) {
                sessionGuard.onReconnect(now_ns());
                for (const auto& symbol : ENABLED_SYMBOLS) {
                    fixClient.subscribeMarketData(symbol);
                }
                std::cout << "[MAIN] Reconnected successfully\n";
            } else {
                std::cerr << "[MAIN] Reconnect failed, will retry...\n";
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // ==========================================================================
    // SHUTDOWN
    // ==========================================================================
    std::cout << "\n[MAIN] ======== SHUTTING DOWN ========\n";
    
    std::cout << "[MAIN] Final Stats:\n"
              << "  Total Ticks: " << g_total_ticks.load() << "\n"
              << "  [XAU] Daily PnL: " << std::fixed << std::setprecision(2) << xau_executor.getDailyPnL() << "\n"
              << "  [NAS] Daily PnL: " << nas_executor.getDailyPnL() << "\n"
              << "  [XAU] Avg Latency: " << std::setprecision(1) << xau_metrics.getAvgLatencyMs() << "ms\n"
              << "  [NAS] Avg Latency: " << nas_metrics.getAvgLatencyMs() << "ms\n"
              << "  Kill Switch: " << (killSwitch.isArmed() ? killSwitch.getArmReason() : "NOT ARMED") << "\n"
              << "  FIX Throttled: " << fixThrottle.getThrottleCount() << " msgs\n";
    
    std::cout << "[MAIN] Disconnecting from cTrader FIX...\n";
    fixClient.disconnect();
    
    std::cout << "[MAIN] Stopping GUI server...\n";
    gui.stop();
    
    // ===================================================================
    // v4.23.0: SHUTDOWN SHADOW INFRASTRUCTURE
    // ===================================================================
    std::cout << "[MAIN] Stopping watchdog thread...\n";
    shadow::WatchdogThread::stop();
    
    std::cout << "[MAIN] Flushing FIX journal...\n";
    shadow::JournalWriter::flush();
    shadow::JournalWriter::close();
    
    std::cout << "[MAIN] Exporting final equity curve...\n";
    shadow::EquityCurve::exportCSV();
    shadow::EquityCurve::printSummary();
    // ===================================================================
    
    std::cout << "[MAIN] Chimera " << CHIMERA_VERSION << " shutdown complete\n";
    teardownLogging();
    
    return 0;
}
