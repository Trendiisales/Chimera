// =============================================================================
// main_triple.cpp - Chimera v4.6.0 - Quad Engine Entry Point
// =============================================================================
// ARCHITECTURE (4 execution layers):
//   - BinanceEngine: CPU 1, Crypto via WebSocket (Alpha trades)
//   - CfdEngine: CPU 2, CFD/Forex via FIX 4.4 (Alpha trades)
//   - IncomeEngine: CPU 3, Income/Yield trades (behavior-based)
//   - MLEngine: CPU 4, ML Gate + Attribution + Drift Guard (quality control)
//   - They share ONLY GlobalKill and DailyLossGuard (atomics)
//   - GUIBroadcaster: WebSocket server for React dashboard (port 7777)
//
// v4.6.0 CHANGES:
//   - Full ML Execution System (VETO + SIZE SCALER, not signal generator)
//   - Regime-specific quantile models (TREND, MEANREV, BURST, DEAD)
//   - Session-aware thresholds (ASIA, LONDON, NY)
//   - Gold-specific pyramiding (XAUUSD + BURST + NY only)
//   - ML Attribution logging (per-trade ML metrics)
//   - Drift detection & kill switch
//   - Venue routing (FIX vs CFD based on risk)
//
// EXECUTION FLOW:
//   Rule Engine proposes trade
//     → MLGate.evaluate()
//       → Distribution checks (quantiles)
//         → Latency-aware threshold
//           → Size scaling
//             → Venue selection
//               → Submit or reject
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
#include <sys/stat.h>

// =============================================================================
// AUTO-LOGGING SYSTEM - Tee all output to file automatically
// =============================================================================
class TeeStreambuf : public std::streambuf {
private:
    std::streambuf* console_buf_;
    std::ofstream file_;
    
public:
    TeeStreambuf(std::streambuf* console_buf, const std::string& filename)
        : console_buf_(console_buf) {
        file_.open(filename, std::ios::out | std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "[LOG] WARNING: Could not open log file: " << filename << "\n";
        }
    }
    
    ~TeeStreambuf() {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    bool is_open() const { return file_.is_open(); }
    
protected:
    virtual int overflow(int c) override {
        if (c != EOF) {
            if (console_buf_) {
                console_buf_->sputc(c);
            }
            if (file_.is_open()) {
                file_.put(c);
                if (c == '\n') {
                    file_.flush();
                }
            }
        }
        return c;
    }
    
    virtual int sync() override {
        if (console_buf_) {
            console_buf_->pubsync();
        }
        if (file_.is_open()) {
            file_.flush();
        }
        return 0;
    }
};

class AutoLogger {
private:
    std::unique_ptr<TeeStreambuf> cout_tee_;
    std::unique_ptr<TeeStreambuf> cerr_tee_;
    std::streambuf* original_cout_;
    std::streambuf* original_cerr_;
    std::string log_filename_;
    
public:
    AutoLogger() : original_cout_(nullptr), original_cerr_(nullptr) {}
    
    bool init() {
        const char* log_dir = "logs";
        mkdir(log_dir, 0755);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        std::ostringstream filename;
        filename << log_dir << "/chimera_"
                 << std::put_time(tm, "%Y%m%d_%H%M%S") << ".log";
        log_filename_ = filename.str();
        
        original_cout_ = std::cout.rdbuf();
        original_cerr_ = std::cerr.rdbuf();
        
        cout_tee_ = std::make_unique<TeeStreambuf>(original_cout_, log_filename_);
        cerr_tee_ = std::make_unique<TeeStreambuf>(original_cerr_, log_filename_);
        
        if (!cout_tee_->is_open()) {
            return false;
        }
        
        std::cout.rdbuf(cout_tee_.get());
        std::cerr.rdbuf(cerr_tee_.get());
        
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        std::cout << "  CHIMERA AUTO-LOG STARTED: " << log_filename_ << "\n";
        std::cout << "  Timestamp: " << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n\n";
        
        return true;
    }
    
    ~AutoLogger() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        std::cout << "\n═══════════════════════════════════════════════════════════════\n";
        std::cout << "  SESSION ENDED: " << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "\n";
        std::cout << "  Log saved: " << log_filename_ << "\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        
        if (original_cout_) {
            std::cout.rdbuf(original_cout_);
        }
        if (original_cerr_) {
            std::cerr.rdbuf(original_cerr_);
        }
    }
    
    const std::string& getLogFilename() const { return log_filename_; }
};

static AutoLogger g_auto_logger;

// =============================================================================
// SHARED STATE (Atomics Only)
// =============================================================================
#include "shared/GlobalKill.hpp"
#include "shared/DailyLossGuard.hpp"

// =============================================================================
// CRYPTO ENGINE (Binance) - Alpha Trades
// =============================================================================
#include "binance/BinanceEngine.hpp"
#include "binance/BinanceConfig.hpp"
#include "CryptoRuleset.hpp"  // v4.5.0: Official crypto trading ruleset
#include "CryptoEngineV2.hpp" // v4.5.1: Clean crypto engine implementation

// =============================================================================
// CFD ENGINE (cTrader) - Alpha Trades
// =============================================================================
#include "CfdEngine.hpp"

// =============================================================================
// INCOME ENGINE - Behavior-Based Income Trades
// =============================================================================
#include "IncomeEngine.hpp"

// =============================================================================
// ENGINE OWNERSHIP (v4.5.0) - Engine-level symbol isolation
// =============================================================================
#include "core/EngineOwnership.hpp"

// =============================================================================
// GLOBAL RISK GOVERNOR (v4.5.1) - Unified risk control
// =============================================================================
#include "shared/GlobalRiskGovernor.hpp"

// =============================================================================
// EXECUTION AUTHORITY (v4.7.0) - THE SINGLE CHOKE POINT
// =============================================================================
#include "core/ExecutionAuthority.hpp"

// =============================================================================
// SCALP PROFILE SYSTEM (v4.7.0) - DUAL SCALP (NY + LONDON)
// =============================================================================
#include "core/ScalpProfile.hpp"

// =============================================================================
// GUI BROADCASTER (WebSocket server for React dashboard)
// =============================================================================
#include "gui/GUIBroadcaster.hpp"
#include "shared/SymbolEnabledManager.hpp"

// =============================================================================
// ML EXECUTION SYSTEM (v4.6.0) - Full ML gate pipeline
// =============================================================================
#include "ml/MLFeatureLogger.hpp"
#include "ml/MLTypes.hpp"
#include "ml/MLModel.hpp"
#include "ml/MLGate.hpp"
#include "ml/MLAttribution.hpp"
#include "ml/MLDriftGuard.hpp"
#include "ml/GoldPyramiding.hpp"
#include "ml/MLVenueRouter.hpp"
#include "ml/MLMetricsPublisher.hpp"

// =============================================================================
// GLOBAL STATE
// =============================================================================
std::atomic<bool> g_running{true};
std::atomic<int> g_signal_count{0};

// Chimera namespace globals (used by BinanceEngine + IncomeEngine)
Chimera::GlobalKill g_kill;
Chimera::DailyLossGuard g_daily_loss(-200.0);  // v4.5.1: Hard cap -$200 NZD (was -$500)

// Omega namespace globals (used by CfdEngine)
Omega::GlobalKillSwitch g_omega_kill;

// GUI Broadcaster (WebSocket server)
Chimera::GUIBroadcaster g_gui;

// v4.6.0: ML Feature Logger (binary logging for offline analysis)
Chimera::ML::MLFeatureLogger g_ml_logger("ml_features.bin");
std::atomic<uint64_t> g_ml_features_logged{0};
std::atomic<uint64_t> g_ml_trades_logged{0};

// v4.6.0: ML Attribution Logger (per-trade ML metrics)
// Note: Uses global getter getMLAttributionLogger() - no explicit instance needed here

// Forward declarations for engines (needed for signal handler)
Chimera::Binance::BinanceEngine* g_binance_ptr = nullptr;
Omega::CfdEngine* g_cfd_ptr = nullptr;
Chimera::Income::IncomeEngine* g_income_ptr = nullptr;

// =============================================================================
// SIGNAL HANDLER - Aggressive shutdown
// =============================================================================
void signalHandler(int sig) {
    int count = ++g_signal_count;
    
    if (count == 1) {
        std::cout << "\n[CHIMERA] Signal " << sig << " received - initiating graceful shutdown...\n";
        std::cout << "[CHIMERA] Press Ctrl+C again to force immediate exit.\n";
        g_running = false;
        g_kill.kill();
        g_omega_kill.triggerAll();
        
        // Immediately stop all engines
        if (g_income_ptr) {
            std::cout << "[CHIMERA] Stopping Income engine immediately...\n";
            g_income_ptr->stop();
        }
        if (g_cfd_ptr) {
            std::cout << "[CHIMERA] Stopping CFD engine immediately...\n";
            g_cfd_ptr->stop();
        }
        if (g_binance_ptr) {
            std::cout << "[CHIMERA] Stopping Binance engine immediately...\n";
            g_binance_ptr->stop();
        }
    } else if (count == 2) {
        std::cout << "\n[CHIMERA] Second signal - forcing exit in 2 seconds...\n";
        std::thread([](){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "[CHIMERA] Force exit!\n";
            std::_Exit(1);
        }).detach();
    } else {
        std::cout << "\n[CHIMERA] Immediate force exit!\n";
        std::_Exit(1);
    }
}

// =============================================================================
// SINGLETON CHECK
// =============================================================================
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <cstdlib>
#include <cstdio>

static int g_lock_fd = -1;
static const char* LOCK_FILE = "/tmp/chimera.lock";

bool acquireSingletonLock() {
    g_lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (g_lock_fd < 0) {
        std::cerr << "[CHIMERA] ERROR: Cannot create lock file\n";
        return false;
    }
    
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        char buf[32] = {0};
        lseek(g_lock_fd, 0, SEEK_SET);
        ssize_t n = read(g_lock_fd, buf, sizeof(buf)-1);
        if (n > 0) {
            int old_pid = atoi(buf);
            if (old_pid > 0) {
                std::cout << "[CHIMERA] Killing existing instance (PID " << old_pid << ")...\n";
                kill(old_pid, SIGTERM);
                usleep(500000);
                kill(old_pid, SIGKILL);
                usleep(200000);
            }
        }
        
        if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
            std::cerr << "[CHIMERA] ERROR: Cannot acquire lock - another instance may still be running\n";
            close(g_lock_fd);
            return false;
        }
    }
    
    if (ftruncate(g_lock_fd, 0) < 0) { /* ignore */ }
    lseek(g_lock_fd, 0, SEEK_SET);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    ssize_t written __attribute__((unused)) = write(g_lock_fd, pid_str, strlen(pid_str));
    
    std::cout << "[CHIMERA] Singleton lock acquired (PID " << getpid() << ")\n";
    return true;
}

void releaseSingletonLock() {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        unlink(LOCK_FILE);
        g_lock_fd = -1;
    }
}
#else
bool acquireSingletonLock() { return true; }
void releaseSingletonLock() {}
#endif

// =============================================================================
// MAIN
// =============================================================================
int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    
    // =========================================================================
    // AUTO-LOGGING
    // =========================================================================
    if (!g_auto_logger.init()) {
        std::cerr << "[CHIMERA] WARNING: Auto-logging failed to initialize\n";
    }
    
    // Print trade mode banner
    Chimera::Binance::print_trade_mode_banner();
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA v4.5.1 - TRIPLE ENGINE + NAS100 OWNERSHIP + CRYPTO RULESET\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  ENGINE 1: Binance (Crypto Alpha)   - OPPORTUNISTIC MODE\n";
    std::cout << "            + Official CryptoRuleset (G1-G5 gates, Class A/B only)\n";
    std::cout << "            + Symbols: BTCUSDT, ETHUSDT ONLY\n";
    std::cout << "  ENGINE 2: cTrader (CFD Alpha)      - LIVE MODE\n";
    std::cout << "            + NAS100: TIME-BASED ownership (soldier outside income)\n";
    std::cout << "  ENGINE 3: Income (ML-Filtered)     - LIVE MODE\n";
    std::cout << "            + NAS100: EXCLUSIVE 03:00-05:00 NY (sniper)\n";
    std::cout << "  NEW: Time-based NAS100 ownership with forced flat\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Dashboard: http://YOUR_VPS_IP:8080/\n";
    std::cout << "  WebSocket: ws://YOUR_VPS_IP:7777\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    // Singleton check
    if (!acquireSingletonLock()) {
        std::cerr << "[CHIMERA] FATAL: Could not acquire singleton lock. Exiting.\n";
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    
    // =========================================================================
    // LOAD CONFIG.INI (equity, risk limits, etc.)
    // =========================================================================
    std::cout << "[CHIMERA] Loading config.ini...\n";
    auto& config = Chimera::ConfigLoader::instance();
    if (!config.load()) {
        std::cerr << "[CHIMERA] WARNING: Could not load config.ini, using defaults\n";
    }
    
    // Read equity values from config (with sensible defaults)
    const double crypto_equity = config.getDouble("trading", "crypto_equity", 15000.0);
    const double cfd_equity = config.getDouble("trading", "cfd_equity", 50000.0);
    const double income_equity = config.getDouble("trading", "income_equity", 100000.0);
    
    std::cout << "[CHIMERA] Equity config loaded:\n";
    std::cout << "  - Crypto: $" << crypto_equity << "\n";
    std::cout << "  - CFD:    $" << cfd_equity << "\n";
    std::cout << "  - Income: $" << income_equity << "\n";
    
    // =========================================================================
    // LOAD TRADING CONFIG
    // =========================================================================
    std::cout << "[CHIMERA] Loading trading config...\n";
    Chimera::getTradingConfig().loadFromFile("chimera_config.json");
    
    // =========================================================================
    // CONFIGURE ENGINE OWNERSHIP (v4.5.0)
    // =========================================================================
    // This enforces strict symbol isolation between engines
    // DENY-BY-DEFAULT: unconfigured engine+symbol = BLOCKED
    std::cout << "[CHIMERA] Configuring engine ownership...\n";
    auto& ownership = Chimera::EngineOwnership::instance();
    
    // Set enforcement mode based on trading mode
    // - DEMO: Log + block (for testing/shadow) - DEFAULT
    // - LIVE: Throw/abort (for production)
    // Check if any engine is in live mode
    bool any_live = true;  // v4.6.0: LIVE MODE ENABLED
    if (any_live) {
        ownership.setEnforcementMode(Chimera::EnforcementMode::LIVE);
    } else {
        ownership.setEnforcementMode(Chimera::EnforcementMode::DEMO);
    }
    
    // For income testing phase: Remove NAS100 from CFD to avoid conflicts
    // Uncomment the next line to enable strict isolation:
    // ownership.removeAllowedSymbol(Chimera::EngineId::CFD, "NAS100");
    
    // Print current ownership configuration
    ownership.printConfig();
    
    // =========================================================================
    // INITIALIZE GLOBAL RISK GOVERNOR (v4.5.1)
    // =========================================================================
    // This enforces:
    //   - Hard daily loss cap: -$200 NZD
    //   - Per-engine risk limits (Income 0.5%, CFD 0.25%, Crypto 0.05%)
    //   - Aggression control based on IncomeEngine outcome
    //   - Auto-shutdown on failure conditions
    std::cout << "[CHIMERA] Initializing Global Risk Governor...\n";
    auto& risk_governor = Chimera::GlobalRiskGovernor::instance();
    risk_governor.init(&g_daily_loss, &g_kill, 15000.0);  // $15k NZD capital
    
    // =========================================================================
    // START GUI BROADCASTER
    // =========================================================================
    std::cout << "[CHIMERA] Starting GUI WebSocket server...\n";
    g_gui.initSymbols();
    g_gui.setKillSwitch(&g_kill);
    g_gui.setVersion("v4.6.0-SPEED-ML");
    if (!g_gui.start()) {
        std::cerr << "[CHIMERA] WARNING: GUI server failed to start (continuing anyway)\n";
    } else {
        std::cout << "[CHIMERA] GUI server started on port 7777\n";
    }
    
    // =========================================================================
    // START ML FEATURE LOGGER (v4.6.0)
    // =========================================================================
    std::cout << "[CHIMERA] Starting ML Feature Logger...\n";
    if (!g_ml_logger.start()) {
        std::cerr << "[CHIMERA] WARNING: ML Feature Logger failed to start\n";
    } else {
        std::cout << "[CHIMERA] ML Feature Logger started - logging to ml_features.bin\n";
    }
    
    // =========================================================================
    // START ML ATTRIBUTION LOGGER (v4.6.0) - Per-trade ML metrics
    // =========================================================================
    std::cout << "[CHIMERA] Starting ML Attribution Logger...\n";
    if (!Chimera::ML::getMLAttributionLogger().start()) {
        std::cerr << "[CHIMERA] WARNING: ML Attribution Logger failed to start\n";
    } else {
        std::cout << "[CHIMERA] ML Attribution Logger started - logging to ml_attribution.bin\n";
    }
    
    // =========================================================================
    // INITIALIZE ML DRIFT GUARD (v4.6.0) - Watches ML health
    // =========================================================================
    std::cout << "[CHIMERA] Initializing ML Drift Guard...\n";
    auto& ml_drift_guard = Chimera::ML::getMLDriftGuard();
    ml_drift_guard.reset();
    std::cout << "[CHIMERA] ML Drift Guard initialized\n";
    
    // Initialize ML Gate
    auto& ml_gate = Chimera::ML::getMLGate();
    ml_gate.reset();
    std::cout << "[CHIMERA] ML Gate initialized (VETO + SIZE SCALER mode)\n";
    
    // =========================================================================
    // SCALP PROFILE SYSTEM (v4.7.0) - DUAL SCALP (NY + LONDON)
    // =========================================================================
    std::cout << "[CHIMERA] Initializing Scalp Profile System...\n";
    Chimera::resetScalpDay();  // Reset daily counters
    std::cout << "[CHIMERA] Scalp Profile System initialized:\n"
              << "  SCALP-NY:  NAS100 edge=0.55 pers=0.40 | XAUUSD edge=0.60 pers=0.45\n"
              << "  SCALP-LDN: NAS100 edge=0.65 pers=0.50 | XAUUSD edge=0.70 pers=0.55\n"
              << "  Daily Limits: loss=-0.50R trades=25 consec=5\n"
              << "  Risk: NY=0.30×CORE LDN=0.20×CORE\n";
    
    // Initialize Gold Pyramid Guard
    auto& gold_pyramid = Chimera::ML::getGoldPyramidGuard();
    std::cout << "[CHIMERA] Gold Pyramid Guard initialized (max levels=" 
              << gold_pyramid.config().max_pyramid_levels << ")\n";
    
    // Initialize ML Venue Router
    auto& ml_venue_router = Chimera::ML::getMLVenueRouter();
    std::cout << "[CHIMERA] ML Venue Router initialized (tail_thresh=" 
              << ml_venue_router.config().tail_risk_threshold << ")\n";
    
    // Initialize ML Metrics Publisher
    auto& ml_metrics = Chimera::ML::getMLMetricsPublisher();
    std::cout << "[CHIMERA] ML Metrics Publisher initialized (max_symbols=" 
              << ml_metrics.symbolCount() << "/" << Chimera::ML::MLMetricsPublisher::MAX_SYMBOLS << ")\n";
    
    // =========================================================================
    // CREATE BINANCE ENGINE (CPU 1) - Alpha Trades
    // =========================================================================
    std::cout << "[CHIMERA] Creating Binance Engine (Alpha)...\n";
    Chimera::Binance::BinanceEngine binance_engine(g_kill, g_daily_loss);
    g_binance_ptr = &binance_engine;
    
    // v4.5.0: Initialize Crypto Ruleset (LIVE mode)
    auto& crypto_ruleset = Chimera::Crypto::getCryptoRuleset();
    crypto_ruleset.enable();  // Enable ruleset (starts in SHADOW)
    crypto_ruleset.markShadowValidated();  // v4.6.0: Mark as validated
    crypto_ruleset.graduateToLive();  // v4.6.0: GRADUATE TO LIVE
    std::cout << "[CHIMERA] Crypto Ruleset initialized and GRADUATED to LIVE mode\n";
    
    // v4.5.1: Initialize clean CryptoEngine (STUB mode by default)
    Chimera::Crypto::CryptoEngine crypto_engine_v2(Chimera::Crypto::CryptoMode::OPPORTUNISTIC);
    // Set execution mode (LIVE)
    Chimera::Crypto::CryptoExecution::setLiveMode(true);
    std::cout << "[CHIMERA] CryptoEngineV2 initialized in OPPORTUNISTIC mode - LIVE TRADING ENABLED\n";
    
    // v4.5.0: Set up tick callback for GUI updates + CryptoEngineV2
    binance_engine.setTickCallback([&crypto_engine_v2](const char* symbol, double bid, double ask,
                                      double bid_qty, double ask_qty, double latency_ms) {
        // Update GUI with crypto tick data
        double spread = ask - bid;
        double mid = (bid + ask) / 2.0;
        
        // Update symbol tick in GUI
        g_gui.updateSymbolTick(symbol, bid, ask, latency_ms);
        
        // Calculate simple imbalance for microstructure display
        double total_qty = bid_qty + ask_qty;
        double imbalance = total_qty > 0 ? (bid_qty - ask_qty) / total_qty : 0.0;
        
        // Update microstructure display (simplified for crypto)
        // OFI = imbalance, VPIN = 0.5 (neutral), pressure = imbalance * 2
        g_gui.updateMicro(imbalance, 0.5, imbalance * 2.0, spread, bid, ask, symbol);
        
        // Record latency in crypto ruleset (for G1 gate)
        Chimera::Crypto::getCryptoRuleset().recordLatency(latency_ms);
        
        // v4.5.1: Feed tick to CryptoEngineV2
        // Get current time in ms
        auto now = std::chrono::steady_clock::now();
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        // Feed to CryptoEngineV2 (only BTCUSDT/ETHUSDT will be processed)
        crypto_engine_v2.onTick(symbol, mid, spread, mid,  // vwap = mid for now
                               bid_qty, ask_qty, latency_ms, now_ms);
    });
    
    std::cout << "[CHIMERA] Binance Engine created\n";
    
    // =========================================================================
    // CREATE CFD ENGINE (CPU 2) - Alpha Trades
    // =========================================================================
    std::cout << "[CHIMERA] Creating CFD Engine (Alpha)...\n";
    Omega::CfdEngine cfd_engine;
    g_cfd_ptr = &cfd_engine;
    
    Chimera::FIXConfig fix_config;
    cfd_engine.setFIXConfig(fix_config);
    cfd_engine.setKillSwitch(&g_omega_kill);
    cfd_engine.setForexSymbols({"EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCAD", "AUDNZD", "USDCHF"});
    cfd_engine.setMetalsSymbols({"XAUUSD", "XAGUSD"});
    cfd_engine.setIndicesSymbols({"US30", "NAS100", "SPX500"});
    
    // CFD order callback with ML trade logging
    cfd_engine.setOrderCallback([](const char* symbol, int8_t side, double qty, double price, double pnl) {
        // Static map to track entry timestamps for hold time calculation
        static std::unordered_map<std::string, uint64_t> entry_timestamps;
        
        std::cout << "[CFD-ALPHA] Order: " << symbol 
                  << " side=" << (side > 0 ? "BUY" : "SELL")
                  << " qty=" << qty 
                  << " price=" << price;
        if (pnl != 0.0) std::cout << " pnl=" << pnl;
        std::cout << "\n";
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl);
        
        // v4.6.0: Log ML trade record
        auto now = std::chrono::steady_clock::now();
        uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        // Calculate R-multiple from PnL (assume 1R = $20 risk for CFD)
        float realized_R = pnl != 0.0 ? static_cast<float>(pnl / 20.0) : 0.0f;
        
        // Log trade (entry if pnl=0, exit if pnl!=0)
        if (pnl == 0.0) {
            // Entry - record timestamp for hold time tracking
            entry_timestamps[symbol] = ts_ns;
            
            // Entry - no outcome yet (simplified 12-param API)
            g_ml_logger.logEntry(
                ts_ns, 
                Chimera::ML::symbolToId(symbol),
                Chimera::ML::MLMarketState::TRENDING,  // Assume trending for scalp
                Chimera::ML::MLTradeIntent::MOMENTUM,
                Chimera::ML::MLRegime::NORMAL_VOL,
                0.0f, 0.5f,               // ofi, vpin
                5.0f,                     // conviction (traded = high)
                1.0f,                     // spread_bps
                static_cast<uint16_t>(0), // minutes_from_open
                side,
                static_cast<uint8_t>(1)   // strategy_id = PureScalper
            );
        } else {
            // Exit - calculate hold time from entry timestamp
            uint32_t hold_time_ms = 0;
            auto it = entry_timestamps.find(symbol);
            if (it != entry_timestamps.end()) {
                uint64_t entry_ns = it->second;
                hold_time_ms = static_cast<uint32_t>((ts_ns - entry_ns) / 1'000'000ULL);
                entry_timestamps.erase(it);  // Clear entry timestamp
            }
            
            // Exit - with outcomes (simplified 16-param API)
            g_ml_logger.logClose(
                ts_ns,
                Chimera::ML::symbolToId(symbol),
                Chimera::ML::MLMarketState::TRENDING,
                Chimera::ML::MLTradeIntent::MOMENTUM,
                Chimera::ML::MLRegime::NORMAL_VOL,
                0.0f, 0.5f,               // ofi, vpin
                5.0f,                     // conviction
                1.0f,                     // spread_bps
                static_cast<uint16_t>(0), // minutes_from_open
                side,
                static_cast<uint8_t>(1),  // strategy_id
                realized_R,               // realized R
                std::max(0.0f, realized_R), // mfe_R (estimate)
                std::min(0.0f, realized_R), // mae_R (estimate)
                hold_time_ms              // Real hold time in ms
            );
        }
        g_ml_trades_logged.fetch_add(1, std::memory_order_relaxed);
    });
    
    // CFD tick callback with ML feature logging
    static std::atomic<uint64_t> cfd_tick_count{0};
    cfd_engine.setTickCallback([](const char* symbol, double bid, double ask,
                                   double ofi, double vpin, double pressure, double latency_ms) {
        g_gui.updateMicro(ofi, vpin, pressure, ask - bid, bid, ask, symbol);
        g_gui.updateSymbolTick(symbol, bid, ask, latency_ms);
        
        // v4.6.0: Log ML features (sampled - every 100th tick per symbol)
        uint64_t tick_num = ++cfd_tick_count;
        if (tick_num % 100 == 0) {
            auto now = std::chrono::steady_clock::now();
            uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();
            
            double mid = (bid + ask) / 2.0;
            double spread_bps = (ask - bid) / mid * 10000.0;
            
            // Determine regime from spread and vpin
            Chimera::ML::MLRegime regime = Chimera::ML::MLRegime::NORMAL_VOL;
            if (vpin > 0.7) regime = Chimera::ML::MLRegime::HIGH_VOL;
            if (vpin > 0.85) regime = Chimera::ML::MLRegime::CRISIS;
            if (vpin < 0.3 && spread_bps < 2.0) regime = Chimera::ML::MLRegime::LOW_VOL;
            
            // Determine state from OFI and pressure
            Chimera::ML::MLMarketState state = Chimera::ML::MLMarketState::RANGING;
            if (std::fabs(ofi) > 0.3) state = Chimera::ML::MLMarketState::TRENDING;
            if (vpin > 0.6) state = Chimera::ML::MLMarketState::VOLATILE;
            
            // Simplified 12-param logEntry API
            g_ml_logger.logEntry(
                ts_ns, 
                Chimera::ML::symbolToId(symbol),
                state,
                Chimera::ML::MLTradeIntent::NO_TRADE,  // Tick snapshot, not a trade
                regime,
                static_cast<float>(ofi),
                static_cast<float>(vpin),
                0.0f,                    // conviction (no trade)
                static_cast<float>(spread_bps),
                static_cast<uint16_t>(0), // minutes_from_open
                static_cast<int8_t>(0),   // side (0 = no trade)
                static_cast<uint8_t>(0)   // strategy_id
            );
            g_ml_features_logged.fetch_add(1, std::memory_order_relaxed);
        }
    });
    
    std::cout << "[CHIMERA] CFD Engine created\n";
    
    // =========================================================================
    // CREATE INCOME ENGINE (CPU 3) - Behavior-Based Income
    // =========================================================================
    std::cout << "[CHIMERA] Creating Income Engine (ML-Filtered)...\n";
    Chimera::Income::IncomeEngine income_engine(g_kill, g_daily_loss);
    g_income_ptr = &income_engine;
    
    // Configure income engine
    Chimera::Income::IncomeConfig income_cfg;
    income_cfg.max_position_size = 0.01;
    // ml_veto_threshold defaults to 0.60 (LOCKED)
    income_cfg.take_profit_bps = 3.0;
    income_cfg.stop_loss_bps = 5.0;
    income_cfg.trade_london = true;
    income_cfg.trade_ny = true;
    income_cfg.trade_asia = false;
    income_engine.set_config(income_cfg);
    
    // Income trade callback
    income_engine.set_trade_callback([](const char* symbol, int8_t side, double qty, double price, double pnl) {
        std::cout << "[INCOME] Trade: " << symbol 
                  << " side=" << (side > 0 ? "BUY" : "SELL")
                  << " qty=" << qty 
                  << " price=" << price;
        if (pnl != 0.0) std::cout << " pnl=" << pnl << " bps";
        std::cout << "\n";
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl);
    });
    
    // Income log callback
    income_engine.set_log_callback([](const char* msg) {
        std::cout << msg << std::endl;
    });
    
    std::cout << "[CHIMERA] Income Engine created\n";
    
    // =========================================================================
    // v4.5.1: SET UP CROSS-ENGINE POSITION CALLBACKS
    // =========================================================================
    // CryptoEngine must check if IncomeEngine or CFDEngine have positions
    // before entering a trade. This prevents crypto from interfering with
    // the primary income streams.
    crypto_engine_v2.setIncomePositionCallback([&income_engine]() -> bool {
        return income_engine.has_position();
    });
    
    crypto_engine_v2.setCFDPositionCallback([&cfd_engine]() -> bool {
        return cfd_engine.hasPosition();
    });
    
    // Set equity for position sizing (from config.ini)
    crypto_engine_v2.setEquity(crypto_equity);
    
    std::cout << "[CHIMERA] CryptoEngineV2 cross-engine callbacks configured\n";
    
    // =========================================================================
    // WIRE CFD TICKS TO INCOME ENGINE (NAS100 ONLY)
    // =========================================================================
    // Income Engine ONLY receives NAS100 ticks
    // XAUUSD and all other symbols are BLOCKED at engine level
    cfd_engine.setTickCallback([&income_engine](const char* symbol, double bid, double ask,
                                   double ofi, double vpin, double pressure, double latency_ms) {
        // Update GUI for all symbols
        g_gui.updateMicro(ofi, vpin, pressure, ask - bid, bid, ask, symbol);
        g_gui.updateSymbolTick(symbol, bid, ask, latency_ms);
        
        // Feed to Income Engine - NAS100 ONLY
        // Engine will also reject anything not NAS100, but filter here too
        if (strcmp(symbol, "NAS100") == 0) {
            auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            // CFD FIX protocol doesn't provide order book depth - use fixed estimates
            // Based on typical NAS100 liquidity during NY session
            double bid_depth = 100.0;
            double ask_depth = 100.0;
            
            income_engine.on_tick(symbol, bid, ask, bid_depth, ask_depth, ofi, vpin, now_ns);
        }
    });
    
    // =========================================================================
    // START ALL ENGINES
    // =========================================================================
    std::cout << "\n[CHIMERA] Starting all engines...\n";
    
    bool binance_ok = binance_engine.start();
    if (!binance_ok) {
        std::cout << "[CHIMERA] WARNING: Binance Engine failed to start (will retry)\n";
    } else {
        std::cout << "[CHIMERA] Binance Engine started\n";
    }
    
    bool cfd_ok = cfd_engine.start();
    if (!cfd_ok) {
        std::cout << "[CHIMERA] WARNING: CFD Engine failed to start (will retry)\n";
    } else {
        std::cout << "[CHIMERA] CFD Engine started\n";
    }
    
    bool income_ok = income_engine.start();
    if (!income_ok) {
        std::cout << "[CHIMERA] WARNING: Income Engine failed to start\n";
    } else {
        std::cout << "[CHIMERA] Income Engine started (LIVE MODE)\n";
    }
    
    g_gui.updateConnections(binance_ok, cfd_ok);
    
    const auto binance_cfg = Chimera::Binance::get_config();
    const char* binance_env = binance_cfg.is_testnet ? "TESTNET" : "LIVE";
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA v4.4 TRIPLE ENGINE RUNNING\n";
    std::cout << "  Binance: " << (binance_ok ? "ACTIVE" : "CONNECTING") << " (" << binance_env << ")\n";
    std::cout << "  cTrader: " << (cfd_ok ? "ACTIVE" : "CONNECTING") << "\n";
    std::cout << "  Income:  " << (income_ok ? "ACTIVE" : "WAITING") << " (LIVE)\n";
    std::cout << "  GUI: ws://localhost:7777\n";
    std::cout << "  Press Ctrl+C to exit\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n" << std::endl;
    
    // =========================================================================
    // MAIN LOOP
    // =========================================================================
    uint64_t loop_count = 0;
    auto loop_start = std::chrono::steady_clock::now();
    (void)loop_start;  // Used for timing reference
    
    try {
        while (g_running && !g_kill.killed()) {
            auto this_loop_start = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ++loop_count;
            
            // ═══════════════════════════════════════════════════════════════
            // HEARTBEAT UPDATE (every loop - v4.6.0)
            // ═══════════════════════════════════════════════════════════════
            auto now = std::chrono::steady_clock::now();
            double loop_ms = std::chrono::duration<double, std::milli>(now - this_loop_start).count();
            double uptime_sec = std::chrono::duration<double>(now - loop_start).count();
            g_gui.updateHeartbeat(loop_count, loop_ms, uptime_sec);
            
            // ═══════════════════════════════════════════════════════════════
            // CONNECTION STATUS UPDATE (every loop - v4.6.0)
            // Track reconnection attempts for dashboard alerts
            // ═══════════════════════════════════════════════════════════════
            {
                bool binance_connected = binance_engine.isConnected();
                bool ctrader_connected = cfd_engine.isConnected();
                uint32_t fix_reconnects = static_cast<uint32_t>(cfd_engine.getStats().fix_reconnects.load());
                g_gui.updateConnections(binance_connected, ctrader_connected, fix_reconnects);
                
                // ═══════════════════════════════════════════════════════════════
                // v4.7.0: EXECUTION AUTHORITY - Intent State Wiring
                // This is THE control point for all execution.
                // Intent is LIVE only when BOTH engines are connected AND risk allows.
                // ═══════════════════════════════════════════════════════════════
                bool risk_allows = g_daily_loss.allow();
                bool intent_is_live = binance_connected && ctrader_connected && risk_allows;
                
                // Wire intent to both engines
                binance_engine.setIntentLive(intent_is_live);
                cfd_engine.setIntentLive(intent_is_live);
                
                // Sync risk state to ExecutionAuthority
                Chimera::getExecutionAuthority().setRiskAllows(risk_allows);
            }
            
            // v4.6.0: Update ML stats in GUI
            g_gui.updateMLStats(
                g_ml_features_logged.load(),
                g_ml_trades_logged.load(),
                g_ml_logger.recordsWritten(),
                g_ml_logger.recordsDropped()
            );
            
            // v4.6.0: Update ML execution stats in GUI
            {
                auto gate_stats = Chimera::ML::getMLGate().getStats();
                auto& drift = Chimera::ML::getMLDriftGuard();
                auto venue_stats = Chimera::ML::getMLVenueRouter().getStats();
                
                g_gui.updateMLExecutionStats(
                    gate_stats.accepts,
                    gate_stats.total_rejects(),
                    gate_stats.accept_rate(),
                    drift.rollingQ50(),
                    drift.rollingQ10(),
                    drift.kill(),
                    drift.throttle(),
                    venue_stats.fix_routed,
                    venue_stats.total_cfd()
                );
            }
            
            // ═══════════════════════════════════════════════════════════════
            // v4.7.0: SCALP PROFILE DAILY STATUS (every 60 seconds)
            // ═══════════════════════════════════════════════════════════════
            if (loop_count % 1200 == 0) {  // Every 60 seconds (50ms * 1200)
                Chimera::ScalpDiagnostics::printDailyStatus();
            }
            
            // ═══════════════════════════════════════════════════════════════
            // FAST DAILY LOSS CHECK (v4.5.1) - Every loop iteration (50ms)
            // This is NON-NEGOTIABLE. Nothing overrides it.
            // ═══════════════════════════════════════════════════════════════
            if (!g_daily_loss.allow()) {
                std::cout << "\n[RISK-GOVERNOR] ══════════════════════════════════════════\n";
                std::cout << "[RISK-GOVERNOR] DAILY LOSS LIMIT HIT: $" << g_daily_loss.pnl() << " NZD\n";
                std::cout << "[RISK-GOVERNOR] SHUTTING DOWN ALL ENGINES IMMEDIATELY\n";
                std::cout << "[RISK-GOVERNOR] ══════════════════════════════════════════\n\n";
                
                Chimera::GlobalRiskGovernor::instance().triggerShutdown(
                    Chimera::ShutdownReason::DAILY_LOSS_LIMIT);
                g_kill.kill();
                break;  // Exit main loop immediately
            }
            
            // ═══════════════════════════════════════════════════════════════
            // NAS100 OWNERSHIP MONITORING (v4.5.1)
            // ═══════════════════════════════════════════════════════════════
            if (loop_count % 20 == 0) {  // Every 1 second
                // Check if CFD needs to force-flat NAS100
                // Note: CFD engine treats NAS100 as SENSOR ONLY (tier 3)
                // Enforcement happens at order submission via canTradeNAS100() check
                // No position closing needed since CFD cannot open NAS100 positions
                if (Chimera::isCFDNAS100ForcedFlat()) {
                    static bool logged_forced_flat = false;
                    if (!logged_forced_flat) {
                        std::cout << "[NAS100-OWNERSHIP] CFD FORCED FLAT PERIOD - No NAS100 trades allowed\n";
                        std::cout << "[NAS100-OWNERSHIP] Enforcement: canTradeNAS100() blocks all orders\n";
                        logged_forced_flat = true;
                    }
                } else {
                    // Reset log flag when we leave forced-flat period
                    static bool logged_forced_flat = false;
                    logged_forced_flat = false;
                }
                
                // Check if income engine should be locked after exit
                if (income_engine.stats().trades_exited.load() > 0 && 
                    Chimera::isIncomeWindowActive() &&
                    !Chimera::EngineOwnership::instance().isIncomeLocked()) {
                    // Income engine traded and exited - lock it
                    Chimera::EngineOwnership::instance().lockIncomeEngine();
                    std::cout << "[NAS100-OWNERSHIP] Income engine LOCKED after trade exit\n";
                }
                
                // Reset income lock at session start (outside income window)
                if (!Chimera::isIncomeWindowActive()) {
                    static int last_ny_hour = -1;
                    int ny_hour = Chimera::getNYHour();
                    
                    // Detect transition out of income window
                    if (last_ny_hour >= 3 && last_ny_hour < 5 && (ny_hour < 3 || ny_hour >= 5)) {
                        Chimera::EngineOwnership::instance().resetDailyState();
                        std::cout << "[NAS100-OWNERSHIP] Daily state reset - new session\n";
                    }
                    last_ny_hour = ny_hour;
                }
            }
            
            // ═══════════════════════════════════════════════════════════════
            // SYNC CRYPTO STRESS TO INCOME ENGINE
            // ═══════════════════════════════════════════════════════════════
            // Read crypto stress from Binance engine latency (proxy for market stress)
            // Elevated latency during crypto volatility spikes = market stress
            if (loop_count % 20 == 0) {  // Every 1 second
                // Crypto stress based on latency - elevated latency indicates market stress
                double avg_latency = binance_engine.avg_latency_ms();
                double crypto_stress = 0.0;
                if (avg_latency > 500.0) crypto_stress = 1.0;        // Severe stress
                else if (avg_latency > 200.0) crypto_stress = 0.5;   // Moderate stress
                else if (avg_latency > 100.0) crypto_stress = 0.2;   // Mild stress
                // else: 0.0 = normal
                
                income_engine.set_crypto_stress(crypto_stress);
                
                // v4.5.0: Sync stress to CryptoRuleset (for G4 gate)
                auto& ruleset = Chimera::Crypto::getCryptoRuleset();
                ruleset.setCryptoStress(crypto_stress);
                
                // Read income engine exposure for cross-asset check
                // Exposure = 0.3 (30% of max risk) when income engine has a position
                double income_exposure = income_engine.has_position() ? 0.3 : 0.0;
                ruleset.setIncomeExposure(income_exposure);
                
                // Equity stress from CFD engine (simplified)
                double equity_stress = 0.0;  // Would read from cfd_engine kill switch level
                ruleset.setEquityStress(equity_stress);
            }
            
            // ═══════════════════════════════════════════════════════════════
            // SYNC KILL SWITCH LEVEL TO INCOME ENGINE
            // ═══════════════════════════════════════════════════════════════
            if (loop_count % 10 == 0) {
                // Read CFD kill switch level and propagate to Income
                auto ks_level = Omega::KillSwitchLevel::NORMAL;  // Would read from CFD
                income_engine.set_killswitch_level(ks_level);
            }
            
            // ═══════════════════════════════════════════════════════════════
            // STATUS UPDATE
            // ═══════════════════════════════════════════════════════════════
            if (loop_count % 1200 == 0) {  // Every 60 seconds
                auto uptime_sec = loop_count * 50 / 1000;
                
                std::cout << "\n[CHIMERA] Status @ " << uptime_sec << "s:\n";
                std::cout << "  Binance: ticks=" << binance_engine.total_ticks()
                          << " orders=" << binance_engine.orders_sent()
                          << " fills=" << binance_engine.orders_filled() << "\n";
                
                const auto& cfd_stats = cfd_engine.getStats();
                std::cout << "  cTrader: ticks=" << cfd_stats.ticks_processed.load()
                          << " orders=" << cfd_stats.orders_sent.load()
                          << " fills=" << cfd_stats.orders_filled.load()
                          << " latency=" << cfd_stats.avgLatencyUs() << "μs\n";
                
                const auto& income_stats = income_engine.stats();
                std::cout << "  Income:  ticks=" << income_stats.ticks_processed.load()
                          << " signals=" << income_stats.signals_generated.load()
                          << " trades=" << income_stats.trades_entered.load()
                          << " winrate=" << std::fixed << std::setprecision(1) 
                          << (income_stats.win_rate() * 100.0) << "%"
                          << " regime_score=" << std::setprecision(2)
                          << income_engine.current_regime_score() << "\n";
                
                // v4.5.0: Crypto Ruleset status
                auto& ruleset = Chimera::Crypto::getCryptoRuleset();
                std::cout << "  Crypto:  state=" << Chimera::Crypto::ruleset_state_str(ruleset.state())
                          << " trades=" << ruleset.tradesToday()
                          << " pnl=$" << std::setprecision(2) << ruleset.dailyPnl()
                          << " streak=" << ruleset.lossStreak()
                          << " block=" << Chimera::Crypto::block_reason_str(ruleset.lastBlockReason()) << "\n";
                
                // v4.5.1: CryptoEngineV2 status
                std::cout << "  CryptoV2: mode=" << Chimera::Crypto::modeStr(crypto_engine_v2.mode())
                          << " state=" << Chimera::Crypto::stateStr(crypto_engine_v2.state())
                          << " block=" << crypto_engine_v2.blockReason() << "\n";
                Chimera::Crypto::CryptoRiskManager::instance().printStatus();
                
                std::cout << "  Combined PnL: $" << g_daily_loss.pnl() << " NZD\n";
                std::cout << "  GUI clients: " << g_gui.clientCount() << std::endl;
                
                // v4.6.0: ML Logger status
                std::cout << "  ML Logger: features=" << g_ml_features_logged.load()
                          << " trades=" << g_ml_trades_logged.load()
                          << " written=" << g_ml_logger.recordsWritten()
                          << " dropped=" << g_ml_logger.recordsDropped() << "\n";
                
                // v4.6.0: ML Gate status
                Chimera::ML::getMLGate().printStats();
                
                // v4.6.0: ML Drift Guard status
                Chimera::ML::getMLDriftGuard().printStatus();
                
                // v4.6.0: ML Attribution Logger status
                Chimera::ML::getMLAttributionLogger().printStats();
                
                // v4.6.0: Gold Pyramiding stats
                Chimera::ML::getGoldPyramidGuard().printStats();
                
                // v4.6.0: ML Venue Router stats
                Chimera::ML::getMLVenueRouter().printStats();
                
                // v4.6.0: ML Metrics Publisher summary
                Chimera::ML::getMLMetricsPublisher().printSummary();
                
                // v4.5.1: Risk Governor Status
                auto& risk_gov = Chimera::GlobalRiskGovernor::instance();
                std::cout << "  Risk: DD=" << std::fixed << std::setprecision(0) 
                          << (risk_gov.drawdownUsed() * 100.0) << "% "
                          << "throttle=" << std::setprecision(2) << risk_gov.throttleFactor()
                          << " aggression=" << Chimera::aggression_str(risk_gov.aggressionState());
                if (risk_gov.isCryptoKilled()) std::cout << " [CRYPTO-KILLED]";
                if (risk_gov.isShutdown()) std::cout << " [SHUTDOWN:" << Chimera::shutdown_reason_str(risk_gov.shutdownReason()) << "]";
                std::cout << "\n";
                
                // v4.5.1: NAS100 Ownership Status
                auto nas_state = Chimera::getNAS100OwnershipState();
                std::cout << "  NAS100: owner=" << Chimera::nas100_owner_str(nas_state.current_owner)
                          << " NY=" << std::setfill('0') << std::setw(2) << nas_state.ny_hour
                          << ":" << std::setw(2) << nas_state.ny_minute;
                if (nas_state.income_window_active) {
                    std::cout << " [INCOME WINDOW " << (nas_state.seconds_in_income_window / 60) << "m left]";
                } else if (nas_state.cfd_no_new_entries) {
                    std::cout << " [CFD WIND-DOWN]";
                } else if (nas_state.seconds_to_income_window < 3600) {
                    std::cout << " [income in " << (nas_state.seconds_to_income_window / 60) << "m]";
                }
                std::cout << std::setfill(' ') << "\n";
                
                // Note: Fast daily loss check is at start of loop (every 50ms)
                // This 60s status update is just for display
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[CHIMERA-FATAL] Main loop exception: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[CHIMERA-FATAL] Main loop unknown exception!\n";
    }
    
    // =========================================================================
    // SHUTDOWN
    // =========================================================================
    std::cout << "\n[CHIMERA] Main loop exited, finalizing shutdown...\n";
    
    g_binance_ptr = nullptr;
    g_cfd_ptr = nullptr;
    g_income_ptr = nullptr;
    
    // v4.6.0: Stop ML loggers first (flushes all pending records)
    std::cout << "[CHIMERA] Stopping ML Feature Logger...\n";
    g_ml_logger.stop();
    
    std::cout << "[CHIMERA] Stopping ML Attribution Logger...\n";
    Chimera::ML::getMLAttributionLogger().stop();
    
    g_gui.stop();
    income_engine.stop();
    binance_engine.stop();
    cfd_engine.stop();
    
    // Final stats
    const auto& cfd_stats = cfd_engine.getStats();
    const auto& income_stats = income_engine.stats();
    
    std::cout << "\n[CHIMERA] Final Statistics:\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  BINANCE ENGINE (Alpha):\n";
    std::cout << "    Ticks processed: " << binance_engine.total_ticks() << "\n";
    std::cout << "    Orders sent:     " << binance_engine.orders_sent() << "\n";
    std::cout << "    Orders filled:   " << binance_engine.orders_filled() << "\n";
    std::cout << "  CTRADER ENGINE (Alpha):\n";
    std::cout << "    Ticks processed: " << cfd_stats.ticks_processed.load() << "\n";
    std::cout << "    Orders sent:     " << cfd_stats.orders_sent.load() << "\n";
    std::cout << "    Orders filled:   " << cfd_stats.orders_filled.load() << "\n";
    std::cout << "  INCOME ENGINE:\n";
    std::cout << "    Ticks processed: " << income_stats.ticks_processed.load() << "\n";
    std::cout << "    Signals:         " << income_stats.signals_generated.load() << "\n";
    std::cout << "    Trades entered:  " << income_stats.trades_entered.load() << "\n";
    std::cout << "    Trades exited:   " << income_stats.trades_exited.load() << "\n";
    std::cout << "    Win rate:        " << std::fixed << std::setprecision(1) 
              << (income_stats.win_rate() * 100.0) << "%\n";
    std::cout << "    Avg PnL:         " << std::setprecision(2) 
              << income_stats.avg_pnl_bps() << " bps\n";
    std::cout << "    Total PnL:       " << (income_stats.total_pnl_bps.load() / 100.0) << " bps\n";
    std::cout << "    ML vetoes:       " << income_stats.ml_vetoes.load() << "\n";
    std::cout << "    Stand-downs:     " << income_stats.stand_down_triggers.load() << "\n";
    std::cout << "  ML FEATURE LOGGER:\n";
    std::cout << "    Features logged: " << g_ml_features_logged.load() << "\n";
    std::cout << "    Trades logged:   " << g_ml_trades_logged.load() << "\n";
    std::cout << "    Records written: " << g_ml_logger.recordsWritten() << "\n";
    std::cout << "    Records dropped: " << g_ml_logger.recordsDropped() << "\n";
    std::cout << "  ML ATTRIBUTION LOGGER:\n";
    std::cout << "    Entries logged:  " << Chimera::ML::getMLAttributionLogger().entriesLogged() << "\n";
    std::cout << "    Closes logged:   " << Chimera::ML::getMLAttributionLogger().closesLogged() << "\n";
    std::cout << "    Win rate:        " << std::fixed << std::setprecision(1) 
              << Chimera::ML::getMLAttributionLogger().winRate() << "%\n";
    std::cout << "  ML GATE:\n";
    auto ml_gate_stats = Chimera::ML::getMLGate().getStats();
    std::cout << "    Accepts:         " << ml_gate_stats.accepts << "\n";
    std::cout << "    Rejects:         " << ml_gate_stats.total_rejects() << "\n";
    std::cout << "    Accept rate:     " << std::fixed << std::setprecision(1) 
              << ml_gate_stats.accept_rate() << "%\n";
    std::cout << "  ML DRIFT GUARD:\n";
    std::cout << "    Samples:         " << Chimera::ML::getMLDriftGuard().samples() << "\n";
    std::cout << "    Rolling q50:     " << std::fixed << std::setprecision(3) 
              << Chimera::ML::getMLDriftGuard().rollingQ50() << "\n";
    std::cout << "    Kill triggered:  " << (Chimera::ML::getMLDriftGuard().kill() ? "YES" : "no") << "\n";
    std::cout << "    Throttle:        " << (Chimera::ML::getMLDriftGuard().throttle() ? "YES" : "no") << "\n";
    std::cout << "  ML VENUE ROUTER:\n";
    auto venue_stats = Chimera::ML::getMLVenueRouter().getStats();
    std::cout << "    FIX routed:      " << venue_stats.fix_routed << " (" 
              << std::fixed << std::setprecision(1) << venue_stats.fix_pct() << "%)\n";
    std::cout << "    CFD fallback:    " << venue_stats.total_cfd() << "\n";
    std::cout << "  ML METRICS:\n";
    std::cout << "    Symbols tracked: " << Chimera::ML::getMLMetricsPublisher().symbolCount() << "\n";
    std::cout << "  COMBINED:\n";
    std::cout << "    Daily PnL:       $" << g_daily_loss.pnl() << " NZD\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    
    std::cout << "\n[CHIMERA] Shutdown complete\n";
    releaseSingletonLock();
    return 0;
}
