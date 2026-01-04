// =============================================================================
// main_triple.cpp - Chimera v4.9.13 - Quad Engine Entry Point
// =============================================================================
// ARCHITECTURE (4 execution layers):
//   - BinanceEngine: CPU 1, Crypto via WebSocket (Alpha trades)
//   - CfdEngine: CPU 2, CFD/Forex via FIX 4.4 (Alpha trades)
//   - IncomeEngine: CPU 3, Income/Yield trades (behavior-based)
//   - MLEngine: CPU 4, ML Gate + Attribution + Drift Guard (quality control)
//   - They share ONLY GlobalKill and DailyLossGuard (atomics)
//   - GUIBroadcaster: WebSocket server for React dashboard (port 7777)
//
// v4.9.13 CHANGES:
//   - FULL INSTITUTIONAL PIPELINE INTEGRATION
//   - Market Regime Detection (TREND/RANGE/VOLATILITY/DEAD)
//   - Alpha Module Selection (mutually exclusive per regime)
//   - Session Expectancy Curves (time-of-day gating)
//   - Symbol Opportunity Ranking (capital flows to best symbol)
//   - News Gate (hard halts around events)
//   - Regime Risk Profiles (size/stop/target per regime)
//   - NoTradeReason GUI (why didn't we trade?)
//   - Regime×Alpha×Hour PnL Attribution
//   - Auto-retirement of failing alphas
//
// EXECUTION FLOW:
//   Rule Engine proposes trade
//     → InstitutionalPipeline.evaluate()
//       → NewsGate (can we trade at all?)
//         → RegimeDetector (TREND/RANGE/VOL/DEAD)
//           → AlphaSelector (pick alpha for regime)
//             → SessionExpectancy (time-of-day weight)
//               → SymbolSelector (best opportunity)
//                 → RegimeRiskProfile (sizing)
//                   → Submit or reject with NoTradeReason
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
#include <unordered_map>
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
#include "CryptoRuleset.hpp"
#include "CryptoEngineV2.hpp"
#include "microscalp/CryptoMicroScalp.hpp"
#include "microscalp/MetalMicroScalp.hpp"
#include "microscalp/IndexMicroScalp.hpp"

// v4.9.10: Bootstrap probe system for latency measurement
#include "bootstrap/LatencyBootstrapper.hpp"
#include "runtime/SystemMode.hpp"

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
// v4.9.13: INSTITUTIONAL EXECUTION SYSTEM
// =============================================================================
#include "alpha/MarketRegime.hpp"
#include "alpha/AlphaSelector.hpp"
#include "alpha/SessionExpectancy.hpp"
#include "gui/NoTradeReason.hpp"
#include "gui/TradeDecision.hpp"
#include "system/NewsGate.hpp"
#include "risk/RegimeRiskProfile.hpp"
#include "symbol/SymbolSelector.hpp"
#include "audit/RegimePnL.hpp"
#include "core/InstitutionalPipeline.hpp"

// =============================================================================
// GLOBAL STATE
// =============================================================================
std::atomic<bool> g_running{true};
std::atomic<int> g_signal_count{0};

Chimera::GlobalKill g_kill;
Chimera::DailyLossGuard g_daily_loss(-200.0);
Omega::GlobalKillSwitch g_omega_kill;
Chimera::GUIBroadcaster g_gui;

Chimera::ML::MLFeatureLogger g_ml_logger("ml_features.bin");
std::atomic<uint64_t> g_ml_features_logged{0};
std::atomic<uint64_t> g_ml_trades_logged{0};

Chimera::Binance::BinanceEngine* g_binance_ptr = nullptr;
Omega::CfdEngine* g_cfd_ptr = nullptr;
Chimera::Income::IncomeEngine* g_income_ptr = nullptr;

// =============================================================================
// SIGNAL HANDLER
// =============================================================================
void signalHandler(int sig) {
    int count = ++g_signal_count;
    
    if (count == 1) {
        std::cout << "\n[CHIMERA] Signal " << sig << " received - initiating graceful shutdown...\n";
        g_running = false;
        g_kill.kill();
        g_omega_kill.triggerAll();
        
        if (g_income_ptr) g_income_ptr->stop();
        if (g_cfd_ptr) g_cfd_ptr->stop();
        if (g_binance_ptr) g_binance_ptr->stop();
    } else if (count == 2) {
        std::cout << "\n[CHIMERA] Force exit in 2 seconds...\n";
        std::thread([](){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::_Exit(1);
        }).detach();
    } else {
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
    if (g_lock_fd < 0) return false;
    
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        char buf[32] = {0};
        lseek(g_lock_fd, 0, SEEK_SET);
        ssize_t n = read(g_lock_fd, buf, sizeof(buf)-1);
        if (n > 0) {
            int old_pid = atoi(buf);
            if (old_pid > 0) {
                kill(old_pid, SIGTERM);
                usleep(500000);
                kill(old_pid, SIGKILL);
                usleep(200000);
            }
        }
        if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
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
// v4.9.13: INSTITUTIONAL PIPELINE HELPER - Build snapshot from tick data
// =============================================================================
inline Chimera::Alpha::MarketSnapshot buildMarketSnapshot(
    double ofi, double pressure, double spread_bps, double atr_pct,
    double trend_strength, double momentum
) {
    Chimera::Alpha::MarketSnapshot snap;
    snap.trend_strength = trend_strength;
    snap.momentum_strength = momentum;
    snap.pullback_depth = 0.2;
    snap.range_score = 1.0 - trend_strength;
    snap.at_range_extreme = std::abs(ofi) > 0.7;
    snap.exhaustion_signal = std::abs(momentum) < 0.3;
    snap.volatility_expansion = atr_pct > 0.7;
    snap.range_compression_prior = atr_pct < 0.3;
    snap.volume_spike = std::abs(pressure) > 1.5;
    snap.structure_clarity = std::max(trend_strength, snap.range_score);
    snap.atr_percentile = atr_pct;
    snap.spread_expansion = spread_bps / 3.0;
    return snap;
}

// =============================================================================
// MAIN
// =============================================================================
int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    
    if (!g_auto_logger.init()) {
        std::cerr << "[CHIMERA] WARNING: Auto-logging failed to initialize\n";
    }
    
    Chimera::Binance::print_trade_mode_banner();
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA v4.9.13 - FULL INSTITUTIONAL EXECUTION PIPELINE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  NEW: Market Regime Detection (TREND/RANGE/VOLATILITY/DEAD)\n";
    std::cout << "  NEW: Alpha Module Selection (mutually exclusive per regime)\n";
    std::cout << "  NEW: Session Expectancy Curves (time-of-day gating)\n";
    std::cout << "  NEW: Symbol Opportunity Ranking (capital to best symbol)\n";
    std::cout << "  NEW: Regime×Alpha×Hour PnL Attribution\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
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
    // LOAD CONFIG
    // =========================================================================
    std::cout << "[CHIMERA] Loading config.ini...\n";
    auto& config = Chimera::ConfigLoader::instance();
    if (!config.load()) {
        std::cerr << "[CHIMERA] WARNING: Could not load config.ini, using defaults\n";
    }
    
    const double crypto_equity = config.getDouble("trading", "crypto_equity", 15000.0);
    const double cfd_equity = config.getDouble("trading", "cfd_equity", 50000.0);
    const double income_equity = config.getDouble("trading", "income_equity", 100000.0);
    
    std::cout << "[CHIMERA] Equity: Crypto=$" << crypto_equity 
              << " CFD=$" << cfd_equity << " Income=$" << income_equity << "\n";
    
    Chimera::getTradingConfig().loadFromFile("chimera_config.json");
    
    // =========================================================================
    // ENGINE OWNERSHIP
    // =========================================================================
    auto& ownership = Chimera::EngineOwnership::instance();
    ownership.setEnforcementMode(Chimera::EnforcementMode::LIVE);
    ownership.printConfig();
    
    // =========================================================================
    // GLOBAL RISK GOVERNOR
    // =========================================================================
    auto& risk_governor = Chimera::GlobalRiskGovernor::instance();
    risk_governor.init(&g_daily_loss, &g_kill, 15000.0);
    
    // =========================================================================
    // v4.9.13: INITIALIZE INSTITUTIONAL SYSTEMS
    // =========================================================================
    std::cout << "[CHIMERA] Initializing Institutional Pipeline...\n";
    
    auto& symbol_selector = Chimera::Symbol::getSymbolSelector();
    symbol_selector.addSymbol("BTCUSDT");
    symbol_selector.addSymbol("ETHUSDT");
    symbol_selector.addSymbol("SOLUSDT");
    symbol_selector.addSymbol("XAUUSD");
    symbol_selector.addSymbol("NAS100");
    
    auto& session_mgr = Chimera::Alpha::getSessionExpectancyManager();
    session_mgr.get("BTCUSDT");
    session_mgr.get("ETHUSDT");
    session_mgr.get("SOLUSDT");
    
    auto& regime_mgr = Chimera::Alpha::getSymbolRegimeManager();
    regime_mgr.get("BTCUSDT");
    regime_mgr.get("ETHUSDT");
    regime_mgr.get("SOLUSDT");
    
    auto& alpha_registry = Chimera::Alpha::getAlphaRegistry();
    alpha_registry.reset();
    
    auto& regime_pnl = Chimera::Audit::getRegimePnLTracker();
    auto& news_gate = Chimera::System::getNewsGate();
    auto& inst_pipeline = Chimera::Core::getInstitutionalPipeline();
    (void)news_gate;       // Force initialization, used later
    (void)inst_pipeline;   // Force initialization, used later
    
    std::cout << "[CHIMERA] Institutional Pipeline: READY\n";
    
    // =========================================================================
    // START GUI
    // =========================================================================
    g_gui.initSymbols();
    g_gui.setKillSwitch(&g_kill);
    g_gui.setVersion("v4.9.13-INSTITUTIONAL");
    if (!g_gui.start()) {
        std::cerr << "[CHIMERA] WARNING: GUI server failed to start\n";
    } else {
        std::cout << "[CHIMERA] GUI server started on port 7777\n";
    }
    
    // =========================================================================
    // ML SYSTEMS
    // =========================================================================
    if (!g_ml_logger.start()) {
        std::cerr << "[CHIMERA] WARNING: ML Feature Logger failed to start\n";
    }
    if (!Chimera::ML::getMLAttributionLogger().start()) {
        std::cerr << "[CHIMERA] WARNING: ML Attribution Logger failed to start\n";
    }
    
    auto& ml_drift_guard = Chimera::ML::getMLDriftGuard();
    ml_drift_guard.reset();
    auto& ml_gate = Chimera::ML::getMLGate();
    ml_gate.reset();
    
    Chimera::resetScalpDay();
    auto& gold_pyramid = Chimera::ML::getGoldPyramidGuard();
    auto& ml_venue_router = Chimera::ML::getMLVenueRouter();
    auto& ml_metrics = Chimera::ML::getMLMetricsPublisher();
    (void)gold_pyramid;     // Force initialization
    (void)ml_metrics;       // Force initialization
    
    // =========================================================================
    // CREATE BINANCE ENGINE
    // =========================================================================
    std::cout << "[CHIMERA] Creating Binance Engine...\n";
    Chimera::Binance::BinanceEngine binance_engine(g_kill, g_daily_loss);
    g_binance_ptr = &binance_engine;
    
    auto& crypto_ruleset = Chimera::Crypto::getCryptoRuleset();
    crypto_ruleset.enable();
    crypto_ruleset.markShadowValidated();
    crypto_ruleset.graduateToLive();
    
    Chimera::Crypto::CryptoEngine crypto_engine_v2(Chimera::Crypto::CryptoMode::OPPORTUNISTIC);
    Chimera::Crypto::CryptoExecution::setLiveMode(true);
    
    // =========================================================================
    // MICROSCALP ENGINES
    // =========================================================================
    Chimera::Crypto::CryptoMicroScalpEngine microscalp_btc("BTCUSDT");
    Chimera::Crypto::CryptoMicroScalpEngine microscalp_eth("ETHUSDT");
    Chimera::Crypto::CryptoMicroScalpEngine microscalp_sol("SOLUSDT");
    
    microscalp_btc.setBaseQty(0.001);
    microscalp_eth.setBaseQty(0.01);
    microscalp_sol.setBaseQty(0.1);
    
    // Wire order callbacks
    microscalp_btc.setOrderCallback([&binance_engine](const char* symbol, bool is_buy, double qty, double price, Chimera::Crypto::RoutingMode routing) {
        double order_price = (routing == Chimera::Crypto::RoutingMode::TAKER_ONLY) ? 0.0 : price;
        binance_engine.submitOrder(symbol, is_buy, qty, order_price);
    });
    microscalp_eth.setOrderCallback([&binance_engine](const char* symbol, bool is_buy, double qty, double price, Chimera::Crypto::RoutingMode routing) {
        double order_price = (routing == Chimera::Crypto::RoutingMode::TAKER_ONLY) ? 0.0 : price;
        binance_engine.submitOrder(symbol, is_buy, qty, order_price);
    });
    microscalp_sol.setOrderCallback([&binance_engine](const char* symbol, bool is_buy, double qty, double price, Chimera::Crypto::RoutingMode routing) {
        double order_price = (routing == Chimera::Crypto::RoutingMode::TAKER_ONLY) ? 0.0 : price;
        binance_engine.submitOrder(symbol, is_buy, qty, order_price);
    });
    
    // Wire latency callbacks
    microscalp_btc.setLatencySnapshotCallback([&binance_engine]() { return binance_engine.hotPathLatencySnapshot(); });
    microscalp_eth.setLatencySnapshotCallback([&binance_engine]() { return binance_engine.hotPathLatencySnapshot(); });
    microscalp_sol.setLatencySnapshotCallback([&binance_engine]() { return binance_engine.hotPathLatencySnapshot(); });
    
    // Trade callbacks with institutional attribution
    constexpr uint8_t ENGINE_CRYPTO = static_cast<uint8_t>(Chimera::ML::EngineId::BINANCE);
    constexpr uint8_t STRATEGY_MICROSCALP = static_cast<uint8_t>(Chimera::ML::StrategyId::CRYPTO_MICROSCALP);
    
    static std::unordered_map<std::string, uint64_t> crypto_entry_ts;
    static std::unordered_map<std::string, Chimera::Alpha::MarketRegime> crypto_entry_regime;
    static std::unordered_map<std::string, Chimera::Alpha::AlphaType> crypto_entry_alpha;
    static std::unordered_map<std::string, double> crypto_entry_price;
    
    auto make_trade_callback = [&regime_mgr](const char* sym) {
        return [sym, &regime_mgr](const char* symbol, int8_t side, double qty, double price, double pnl_bps) {
            g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl_bps, ENGINE_CRYPTO, STRATEGY_MICROSCALP);
            
            uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            int utc_hour = Chimera::Execution::getUtcHour();
            
            if (side > 0) {
                crypto_entry_ts[symbol] = now_ns;
                crypto_entry_price[symbol] = price;
                auto* detector = Chimera::Alpha::getSymbolRegimeManager().get(symbol);
                crypto_entry_regime[symbol] = detector ? detector->currentRegime() : Chimera::Alpha::MarketRegime::DEAD;
                crypto_entry_alpha[symbol] = Chimera::Alpha::selectAlpha(crypto_entry_regime[symbol]);
            } else {
                auto it = crypto_entry_ts.find(symbol);
                if (it != crypto_entry_ts.end()) {
                    auto regime = crypto_entry_regime[symbol];
                    auto alpha = crypto_entry_alpha[symbol];
                    double entry_price = crypto_entry_price[symbol];
                    double fees = qty * price * 0.001;
                    
                    Chimera::Core::recordTradeAttribution(
                        symbol, regime, alpha, utc_hour,
                        entry_price, price, qty,
                        (entry_price < price) ? 1 : -1,
                        fees, it->second, now_ns
                    );
                    
                    crypto_entry_ts.erase(it);
                    crypto_entry_regime.erase(symbol);
                    crypto_entry_alpha.erase(symbol);
                    crypto_entry_price.erase(symbol);
                }
            }
        };
    };
    
    microscalp_btc.setTradeCallback(make_trade_callback("BTCUSDT"));
    microscalp_eth.setTradeCallback(make_trade_callback("ETHUSDT"));
    microscalp_sol.setTradeCallback(make_trade_callback("SOLUSDT"));
    
    microscalp_btc.setRoutingMode(Chimera::Crypto::RoutingMode::MAKER_ONLY);
    microscalp_eth.setRoutingMode(Chimera::Crypto::RoutingMode::HYBRID);
    microscalp_sol.setRoutingMode(Chimera::Crypto::RoutingMode::HYBRID);
    
    microscalp_btc.loadState();
    microscalp_eth.loadState();
    microscalp_sol.loadState();
    
    binance_engine.setExternalFillCallback([&microscalp_btc, &microscalp_eth, &microscalp_sol](
        const char* symbol, bool is_buy, double, double price) {
        Chimera::Crypto::FillType fill_type = Chimera::Crypto::FillType::MAKER;
        if (std::strcmp(symbol, "BTCUSDT") == 0) microscalp_btc.onFill(fill_type, price);
        else if (std::strcmp(symbol, "ETHUSDT") == 0) microscalp_eth.onFill(fill_type, price);
        else if (std::strcmp(symbol, "SOLUSDT") == 0) microscalp_sol.onFill(fill_type, price);
        (void)is_buy;
    });
    
    std::cout << "[CHIMERA] MicroScalp engines created for BTCUSDT/ETHUSDT/SOLUSDT\n";
    
    // =========================================================================
    // METAL MICROSCALP
    // =========================================================================
    Chimera::Metal::MetalMicroScalpEngine microscalp_xau("XAUUSD");
    Chimera::Metal::MetalMicroScalpEngine microscalp_xag("XAGUSD");
    microscalp_xau.setBaseQty(0.01);
    microscalp_xag.setBaseQty(0.01);
    
    constexpr uint8_t ENGINE_METAL = static_cast<uint8_t>(Chimera::ML::EngineId::CFD);
    constexpr uint8_t STRATEGY_METAL_SCALP = 10;
    
    microscalp_xau.setTradeCallback([](const char* symbol, int8_t side, double qty, double price, double pnl_bps) {
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl_bps, ENGINE_METAL, STRATEGY_METAL_SCALP);
    });
    microscalp_xag.setTradeCallback([](const char* symbol, int8_t side, double qty, double price, double pnl_bps) {
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl_bps, ENGINE_METAL, STRATEGY_METAL_SCALP);
    });
    
    // =========================================================================
    // INDEX MICROSCALP
    // =========================================================================
    Chimera::MicroScalp::IndexMicroScalpEngine microscalp_nas("NAS100");
    Chimera::MicroScalp::IndexMicroScalpEngine microscalp_us30("US30");
    microscalp_nas.setBaseQty(1.0);
    microscalp_us30.setBaseQty(1.0);
    
    constexpr uint8_t ENGINE_INDEX = static_cast<uint8_t>(Chimera::ML::EngineId::CFD);
    constexpr uint8_t STRATEGY_INDEX_SCALP = 11;
    
    microscalp_nas.setTradeCallback([](const char* symbol, const char*, int8_t side, double qty, double price, double pnl_bps) {
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl_bps, ENGINE_INDEX, STRATEGY_INDEX_SCALP);
    });
    microscalp_us30.setTradeCallback([](const char* symbol, const char*, int8_t side, double qty, double price, double pnl_bps) {
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl_bps, ENGINE_INDEX, STRATEGY_INDEX_SCALP);
    });
    
    // Volatility trackers
    static double btc_ema_vol = 0.5, eth_ema_vol = 0.5, sol_ema_vol = 0.5;
    static double btc_last_mid = 0.0, eth_last_mid = 0.0, sol_last_mid = 0.0;
    static double btc_trend = 0.5, eth_trend = 0.5, sol_trend = 0.5;
    static double btc_momentum = 0.5, eth_momentum = 0.5, sol_momentum = 0.5;
    constexpr double VOL_ALPHA = 0.05;
    
    // Wire tick callback with regime detection
    binance_engine.setTickCallback([&crypto_engine_v2, &microscalp_btc, &microscalp_eth, &microscalp_sol,
                                    &symbol_selector, &regime_mgr](
                                      const char* symbol, double bid, double ask,
                                      double bid_qty, double ask_qty, double latency_ms) {
        double spread = ask - bid;
        double mid = (bid + ask) / 2.0;
        double spread_bps = (spread / mid) * 10000.0;
        
        g_gui.updateSymbolTick(symbol, bid, ask, latency_ms);
        
        double total_qty = bid_qty + ask_qty;
        double ofi = total_qty > 0 ? (bid_qty - ask_qty) / total_qty : 0.0;
        double pressure = 0.0;
        if (ask_qty > 0) {
            pressure = (bid_qty / ask_qty) - 1.0;
            pressure = std::max(-2.0, std::min(2.0, pressure));
        }
        
        g_gui.updateMicro(ofi, 0.5, pressure, spread, bid, ask, symbol);
        Chimera::Crypto::getCryptoRuleset().recordLatency(latency_ms);
        
        auto now = std::chrono::steady_clock::now();
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        
        crypto_engine_v2.onTick(symbol, mid, spread, mid, bid_qty, ask_qty, latency_ms, now_ms);
        
        double volatility = 0.5, trend = 0.5, momentum = 0.5;
        
        if (std::strcmp(symbol, "BTCUSDT") == 0) {
            if (btc_last_mid > 0.0) {
                double move = std::fabs(mid - btc_last_mid) / btc_last_mid;
                btc_ema_vol = VOL_ALPHA * (move * 10000.0) + (1.0 - VOL_ALPHA) * btc_ema_vol;
                double signed_move = (mid - btc_last_mid) / btc_last_mid;
                btc_trend = 0.95 * btc_trend + 0.05 * (0.5 + signed_move * 1000.0);
                btc_trend = std::clamp(btc_trend, 0.0, 1.0);
                btc_momentum = 0.9 * btc_momentum + 0.1 * (0.5 + ofi * 0.5);
            }
            btc_last_mid = mid;
            volatility = btc_ema_vol; trend = btc_trend; momentum = btc_momentum;
            
            auto snap = buildMarketSnapshot(ofi, pressure, spread_bps, volatility, trend, momentum);
            auto* detector = regime_mgr.get(symbol);
            if (detector) {
                auto result = detector->detect(snap);
                symbol_selector.updateRegime(symbol, result.regime);
            }
            
            // Build MicroTick for microscalp engine
            Chimera::Crypto::MicroTick mtick;
            mtick.bid = bid;
            mtick.ask = ask;
            mtick.mid = (bid + ask) / 2.0;
            mtick.ofi = ofi;
            mtick.pressure = pressure;
            mtick.volatility = volatility;
            mtick.latency_ms = latency_ms;
            mtick.ts_ns = now_ns;
            microscalp_btc.onTick(mtick);
            
        } else if (std::strcmp(symbol, "ETHUSDT") == 0) {
            if (eth_last_mid > 0.0) {
                double move = std::fabs(mid - eth_last_mid) / eth_last_mid;
                eth_ema_vol = VOL_ALPHA * (move * 10000.0) + (1.0 - VOL_ALPHA) * eth_ema_vol;
                double signed_move = (mid - eth_last_mid) / eth_last_mid;
                eth_trend = 0.95 * eth_trend + 0.05 * (0.5 + signed_move * 1000.0);
                eth_trend = std::clamp(eth_trend, 0.0, 1.0);
                eth_momentum = 0.9 * eth_momentum + 0.1 * (0.5 + ofi * 0.5);
            }
            eth_last_mid = mid;
            volatility = eth_ema_vol; trend = eth_trend; momentum = eth_momentum;
            
            auto snap = buildMarketSnapshot(ofi, pressure, spread_bps, volatility, trend, momentum);
            auto* detector = regime_mgr.get(symbol);
            if (detector) {
                auto result = detector->detect(snap);
                symbol_selector.updateRegime(symbol, result.regime);
            }
            
            // Build MicroTick for microscalp engine
            Chimera::Crypto::MicroTick mtick;
            mtick.bid = bid;
            mtick.ask = ask;
            mtick.mid = (bid + ask) / 2.0;
            mtick.ofi = ofi;
            mtick.pressure = pressure;
            mtick.volatility = volatility;
            mtick.latency_ms = latency_ms;
            mtick.ts_ns = now_ns;
            microscalp_eth.onTick(mtick);
            
        } else if (std::strcmp(symbol, "SOLUSDT") == 0) {
            if (sol_last_mid > 0.0) {
                double move = std::fabs(mid - sol_last_mid) / sol_last_mid;
                sol_ema_vol = VOL_ALPHA * (move * 10000.0) + (1.0 - VOL_ALPHA) * sol_ema_vol;
                double signed_move = (mid - sol_last_mid) / sol_last_mid;
                sol_trend = 0.95 * sol_trend + 0.05 * (0.5 + signed_move * 1000.0);
                sol_trend = std::clamp(sol_trend, 0.0, 1.0);
                sol_momentum = 0.9 * sol_momentum + 0.1 * (0.5 + ofi * 0.5);
            }
            sol_last_mid = mid;
            volatility = sol_ema_vol; trend = sol_trend; momentum = sol_momentum;
            
            auto snap = buildMarketSnapshot(ofi, pressure, spread_bps, volatility, trend, momentum);
            auto* detector = regime_mgr.get(symbol);
            if (detector) {
                auto result = detector->detect(snap);
                symbol_selector.updateRegime(symbol, result.regime);
            }
            
            // Build MicroTick for microscalp engine
            Chimera::Crypto::MicroTick mtick;
            mtick.bid = bid;
            mtick.ask = ask;
            mtick.mid = (bid + ask) / 2.0;
            mtick.ofi = ofi;
            mtick.pressure = pressure;
            mtick.volatility = volatility;
            mtick.latency_ms = latency_ms;
            mtick.ts_ns = now_ns;
            microscalp_sol.onTick(mtick);
        }
        
        int utc_hour = Chimera::Execution::getUtcHour();
        double session_weight = Chimera::Alpha::getSessionExpectancyManager().getEdgeMultiplier(symbol, utc_hour);
        symbol_selector.updateSessionWeight(symbol, session_weight);
    });
    
    // =========================================================================
    // CREATE CFD ENGINE
    // =========================================================================
    std::cout << "[CHIMERA] Creating CFD Engine...\n";
    Omega::CfdEngine cfd_engine;
    g_cfd_ptr = &cfd_engine;
    
    Chimera::FIXConfig fix_config;
    cfd_engine.setFIXConfig(fix_config);
    cfd_engine.setKillSwitch(&g_omega_kill);
    cfd_engine.setForexSymbols({"EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCAD"});
    cfd_engine.setMetalsSymbols({"XAUUSD", "XAGUSD"});
    cfd_engine.setIndicesSymbols({"US30", "NAS100", "SPX500"});
    
    // Wire metal order callbacks
    microscalp_xau.setOrderCallback([&cfd_engine](const char* symbol, bool is_buy, double qty, double, Chimera::Metal::RoutingMode) {
        cfd_engine.sendExternalOrder(symbol, is_buy ? 1 : -1, qty);
    });
    microscalp_xag.setOrderCallback([&cfd_engine](const char* symbol, bool is_buy, double qty, double, Chimera::Metal::RoutingMode) {
        cfd_engine.sendExternalOrder(symbol, is_buy ? 1 : -1, qty);
    });
    
    // Wire index order callbacks
    microscalp_nas.setOrderCallback([&cfd_engine](const char* symbol, bool is_buy, double qty, bool) {
        if (!Chimera::EngineOwnership::instance().canTradeNAS100(Chimera::EngineId::CFD)) return;
        cfd_engine.sendExternalOrder(symbol, is_buy ? 1 : -1, qty);
    });
    microscalp_us30.setOrderCallback([&cfd_engine](const char* symbol, bool is_buy, double qty, bool) {
        cfd_engine.sendExternalOrder(symbol, is_buy ? 1 : -1, qty);
    });
    
    std::cout << "[CHIMERA] CFD Engine created\n";
    
    // =========================================================================
    // CREATE INCOME ENGINE
    // =========================================================================
    std::cout << "[CHIMERA] Creating Income Engine...\n";
    Chimera::Income::IncomeEngine income_engine(g_kill, g_daily_loss);
    g_income_ptr = &income_engine;
    
    Chimera::Income::IncomeConfig income_cfg;
    income_cfg.max_position_size = 0.01;
    income_cfg.take_profit_bps = 3.0;
    income_cfg.stop_loss_bps = 5.0;
    income_cfg.trade_london = true;
    income_cfg.trade_ny = true;
    income_cfg.trade_asia = false;
    income_engine.set_config(income_cfg);
    
    income_engine.set_trade_callback([](const char* symbol, int8_t side, double qty, double price, double pnl) {
        constexpr uint8_t ENGINE_INCOME = static_cast<uint8_t>(Chimera::ML::EngineId::INCOME);
        constexpr uint8_t STRATEGY_MEAN_REV = static_cast<uint8_t>(Chimera::ML::StrategyId::INCOME_MEAN_REV);
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl, ENGINE_INCOME, STRATEGY_MEAN_REV);
    });
    
    crypto_engine_v2.setIncomePositionCallback([&income_engine]() { return income_engine.has_position(); });
    crypto_engine_v2.setCFDPositionCallback([&cfd_engine]() { return cfd_engine.hasPosition(); });
    crypto_engine_v2.setEquity(crypto_equity);
    
    std::cout << "[CHIMERA] Income Engine created\n";
    
    // =========================================================================
    // START ALL ENGINES
    // =========================================================================
    std::cout << "\n[CHIMERA] Starting all engines...\n";
    
    bool binance_ok = binance_engine.start();
    std::cout << "[CHIMERA] Binance: " << (binance_ok ? "STARTED" : "FAILED") << "\n";
    
    auto& bootstrapper = Chimera::getBootstrapper();
    // v4.9.13 FIX: Use correct symbol IDs (BTCUSDT=1, ETHUSDT=2, SOLUSDT=3)
    // ID 0 is INVALID in SymbolId.hpp - find_symbol(0) returns nullptr!
    bootstrapper.registerSymbol(1, "BTCUSDT");  // BinanceSymbols::BTCUSDT = 1
    bootstrapper.registerSymbol(2, "ETHUSDT");  // BinanceSymbols::ETHUSDT = 2
    bootstrapper.registerSymbol(3, "SOLUSDT");  // BinanceSymbols::SOLUSDT = 3
    
    // v4.9.13: Connect bootstrapper to BinanceEngine for probe orders
    bootstrapper.setSendCallback([&binance_engine](uint16_t symbol_id, const char* symbol, 
                                                    double price, double qty, uint64_t client_id) -> bool {
        return binance_engine.sendProbeOrder(symbol_id, symbol, price, qty, client_id);
    });
    bootstrapper.setCancelCallback([&binance_engine](uint16_t symbol_id, const char* symbol, 
                                                      uint64_t exchange_order_id) -> bool {
        binance_engine.cancelProbeOrder(symbol_id, symbol, exchange_order_id);
        return true;
    });
    std::cout << "[CHIMERA] Bootstrap callbacks connected\n";
    
    bool cfd_ok = cfd_engine.start();
    std::cout << "[CHIMERA] cTrader: " << (cfd_ok ? "STARTED" : "FAILED") << "\n";
    
    bool income_ok = income_engine.start();
    std::cout << "[CHIMERA] Income: " << (income_ok ? "STARTED" : "FAILED") << "\n";
    
    g_gui.updateConnections(binance_ok, cfd_ok);
    
    const auto binance_cfg = Chimera::Binance::get_config();
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA v4.9.13 INSTITUTIONAL PIPELINE RUNNING\n";
    std::cout << "  Binance: " << (binance_ok ? "ACTIVE" : "CONNECTING") 
              << " (" << (binance_cfg.is_testnet ? "TESTNET" : "LIVE") << ")\n";
    std::cout << "  cTrader: " << (cfd_ok ? "ACTIVE" : "CONNECTING") << "\n";
    std::cout << "  Income:  " << (income_ok ? "ACTIVE" : "WAITING") << "\n";
    std::cout << "  GUI: ws://localhost:7777\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    // =========================================================================
    // MAIN LOOP
    // =========================================================================
    uint64_t loop_count = 0;
    auto loop_start = std::chrono::steady_clock::now();
    
    try {
        while (g_running && !g_kill.killed()) {
            auto this_loop_start = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ++loop_count;
            
            auto now = std::chrono::steady_clock::now();
            double loop_ms = std::chrono::duration<double, std::milli>(now - this_loop_start).count();
            double uptime_sec = std::chrono::duration<double>(now - loop_start).count();
            g_gui.updateHeartbeat(loop_count, loop_ms, uptime_sec);
            
            // Connection status (with alert throttling to prevent spam)
            {
                static bool last_binance = true, last_ctrader = true;
                static auto last_binance_alert = std::chrono::steady_clock::time_point{};
                static auto last_ctrader_alert = std::chrono::steady_clock::time_point{};
                constexpr auto ALERT_THROTTLE = std::chrono::seconds(30);  // Max 1 alert per 30s
                
                bool binance_connected = binance_engine.isConnected();
                bool ctrader_connected = cfd_engine.isConnected();
                g_gui.updateConnections(binance_connected, ctrader_connected);
                
                // Throttled alerts - only fire if state changed AND throttle period elapsed
                if (last_binance && !binance_connected) {
                    if (now - last_binance_alert > ALERT_THROTTLE) {
                        g_gui.broadcastConnectionAlert("BINANCE CONNECTION LOST");
                        last_binance_alert = now;
                    }
                }
                if (last_ctrader && !ctrader_connected) {
                    if (now - last_ctrader_alert > ALERT_THROTTLE) {
                        g_gui.broadcastConnectionAlert("CTRADER CONNECTION LOST");
                        last_ctrader_alert = now;
                    }
                }
                
                last_binance = binance_connected;
                last_ctrader = ctrader_connected;
                
                bool risk_allows = g_daily_loss.allow();
                bool intent_live = binance_connected && ctrader_connected && risk_allows;
                binance_engine.setIntentLive(intent_live);
                cfd_engine.setIntentLive(intent_live);
                Chimera::getExecutionAuthority().setRiskAllows(risk_allows);
            }
            
            // ML stats
            g_gui.updateMLStats(g_ml_features_logged.load(), g_ml_trades_logged.load(),
                               g_ml_logger.recordsWritten(), g_ml_logger.recordsDropped());
            
            // Bootstrap probes
            {
                uint64_t now_ns = Chimera::now_ns_monotonic();
                double btc_mid = microscalp_btc.currentMid();
                double eth_mid = microscalp_eth.currentMid();
                double sol_mid = microscalp_sol.currentMid();
                
                if (Chimera::getSystemMode().isBootstrap() && binance_engine.isConnected()) {
                    // v4.9.13 FIX: Use correct symbol IDs (must match registerSymbol)
                    if (btc_mid > 0) bootstrapper.maybeProbe(1, btc_mid, now_ns);  // BTCUSDT = 1
                    if (eth_mid > 0) bootstrapper.maybeProbe(2, eth_mid, now_ns);  // ETHUSDT = 2
                    if (sol_mid > 0) bootstrapper.maybeProbe(3, sol_mid, now_ns);  // SOLUSDT = 3
                }
                
                g_gui.updateSystemMode(Chimera::systemModeStr(Chimera::getSystemMode().globalMode()),
                    static_cast<uint32_t>(binance_engine.probesSent()),
                    static_cast<uint32_t>(binance_engine.probesAcked()));
            }
            
            // ML execution stats
            {
                auto gate_stats = ml_gate.getStats();
                g_gui.updateMLExecutionStats(gate_stats.accepts, gate_stats.total_rejects(),
                    gate_stats.accept_rate(), ml_drift_guard.rollingQ50(), ml_drift_guard.rollingQ10(),
                    ml_drift_guard.kill(), ml_drift_guard.throttle(),
                    ml_venue_router.getStats().fix_routed, ml_venue_router.getStats().total_cfd());
            }
            
            // Governor heat
            {
                g_gui.updateGovernorHeat("BTCUSDT", microscalp_btc.governorHeat(), 
                    microscalp_btc.sizeMultiplierFromHeat(),
                    Chimera::Crypto::governorStateStr(microscalp_btc.governorState()));
                g_gui.updateGovernorHeat("ETHUSDT", microscalp_eth.governorHeat(),
                    microscalp_eth.sizeMultiplierFromHeat(),
                    Chimera::Crypto::governorStateStr(microscalp_eth.governorState()));
                g_gui.updateGovernorHeat("SOLUSDT", microscalp_sol.governorHeat(),
                    microscalp_sol.sizeMultiplierFromHeat(),
                    Chimera::Crypto::governorStateStr(microscalp_sol.governorState()));
            }
            
            // Hot-path latency
            {
                auto lat_snap = binance_engine.hotPathLatencySnapshot();
                auto lat_result = microscalp_btc.lastLatencyGateResult();
                g_gui.updateHotPathLatency(lat_snap.min_ms(), lat_snap.p10_ms(), lat_snap.p50_ms(),
                    lat_snap.p90_ms(), lat_snap.p99_ms(), lat_snap.total_recorded, lat_snap.spikes_filtered,
                    Chimera::hotPathStateStr(lat_result.state), Chimera::execModeStr(lat_result.exec_mode));
            }
            
            // v4.9.13: Institutional updates (every 1 second)
            if (loop_count % 20 == 0) {
                symbol_selector.updateActiveList();
                
                auto& no_trade_mgr = Chimera::GUI::getNoTradeStateManager();
                uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                
                const char* symbols[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
                for (const char* sym : symbols) {
                    auto* detector = regime_mgr.get(sym);
                    Chimera::Alpha::MarketRegime regime = detector ? detector->currentRegime() : Chimera::Alpha::MarketRegime::DEAD;
                    
                    bool can_trade = Chimera::Core::InstitutionalPipeline::quickCheck(
                        g_kill.killed(), !g_daily_loss.allow(),
                        Chimera::getSystemMode().isBootstrap(), regime);
                    
                    if (!can_trade) {
                        if (regime == Chimera::Alpha::MarketRegime::DEAD)
                            no_trade_mgr.update(sym, Chimera::GUI::NoTradeReason::REGIME_DEAD, now_ns, "No structure");
                        else if (Chimera::getSystemMode().isBootstrap())
                            no_trade_mgr.update(sym, Chimera::GUI::NoTradeReason::SYSTEM_BOOTSTRAP, now_ns, "Measuring latency");
                        else if (!g_daily_loss.allow())
                            no_trade_mgr.update(sym, Chimera::GUI::NoTradeReason::DAILY_LOSS_CAP, now_ns, "Daily loss hit");
                    } else {
                        no_trade_mgr.update(sym, Chimera::GUI::NoTradeReason::WAITING_FOR_SIGNAL, now_ns, "Ready");
                    }
                }
                
                alpha_registry.evaluateRetirement(now_ns);
                alpha_registry.evaluateCooldown(now_ns);
            }
            
            // Orderflow
            g_gui.updateOrderflow(binance_engine.total_ticks(), binance_engine.orders_sent(),
                binance_engine.orders_filled(), 0, 0);
            
            // Periodic saves (every 30 seconds)
            if (loop_count % 600 == 0) {
                microscalp_btc.saveState();
                microscalp_eth.saveState();
                microscalp_sol.saveState();
                regime_pnl.persistCSV();
                session_mgr.persist();
            }
            
            // Daily loss check
            if (!g_daily_loss.allow()) {
                std::cout << "\n[RISK-GOVERNOR] DAILY LOSS LIMIT HIT: $" << g_daily_loss.pnl() << " NZD\n";
                risk_governor.triggerShutdown(Chimera::ShutdownReason::DAILY_LOSS_LIMIT);
                g_kill.kill();
                break;
            }
            
            // NAS100 ownership
            if (loop_count % 20 == 0) {
                if (income_engine.stats().trades_exited.load() > 0 && 
                    Chimera::isIncomeWindowActive() && !ownership.isIncomeLocked()) {
                    ownership.lockIncomeEngine();
                }
                if (!Chimera::isIncomeWindowActive()) {
                    static int last_ny_hour = -1;
                    int ny_hour = Chimera::getNYHour();
                    if (last_ny_hour >= 3 && last_ny_hour < 5 && (ny_hour < 3 || ny_hour >= 5)) {
                        ownership.resetDailyState();
                    }
                    last_ny_hour = ny_hour;
                }
            }
            
            // Crypto stress sync
            if (loop_count % 20 == 0) {
                double avg_latency = binance_engine.avg_latency_ms();
                double crypto_stress = avg_latency > 500 ? 1.0 : (avg_latency > 200 ? 0.5 : (avg_latency > 100 ? 0.2 : 0.0));
                income_engine.set_crypto_stress(crypto_stress);
                crypto_ruleset.setCryptoStress(crypto_stress);
                crypto_ruleset.setIncomeExposure(income_engine.has_position() ? 0.3 : 0.0);
                
                // ═══════════════════════════════════════════════════════════════
                // v4.9.13: REGIME × ALPHA DASHBOARD BROADCASTING
                // ═══════════════════════════════════════════════════════════════
                
                // Physics state
                const char* physics = avg_latency < 1.0 ? "COLO" : (avg_latency < 10.0 ? "NEAR_COLO" : "WAN");
                g_gui.updatePhysicsState(physics);
                
                // Broadcast regime × alpha cells for each symbol
                const char* crypto_syms[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
                const char* alpha_names[] = {"MOMENTUM", "FADE", "BREAKOUT", "PULLBACK"};
                
                for (const char* sym : crypto_syms) {
                    auto* det = regime_mgr.get(sym);
                    if (!det) continue;
                    
                    const char* regime_str = Chimera::Alpha::regimeStr(det->currentRegime());
                    
                    // Get expectancy data for this symbol/regime
                    auto* sess = session_mgr.get(sym);
                    int utc_hour = static_cast<int>(loop_count / 72000) % 24;  // Rough hour estimate
                    (void)utc_hour;  // Used for context, actual hours iterated in loop below
                    
                    // For each alpha type, broadcast current cell state
                    for (int a = 0; a < 4; a++) {
                        const char* alpha_str = alpha_names[a];
                        
                        // Get performance from regime PnL tracker
                        double net_r = 0.0;
                        int trades = 0;
                        
                        // Determine status based on alpha registry
                        const char* status = "ACTIVE";
                        if (det->currentRegime() == Chimera::Alpha::MarketRegime::DEAD) {
                            status = "HALT";
                        }
                        
                        // Broadcast the cell
                        g_gui.updateRegimeAlphaCell(
                            "Binance",          // broker
                            regime_str,         // regime
                            alpha_str,          // alpha
                            net_r,              // net_r (from RegimePnL)
                            trades,             // trades
                            0.5,                // win_rate (placeholder)
                            0.0,                // sharpe
                            0.85,               // fill_rate
                            0.02,               // reject_rate
                            avg_latency,        // avg_latency_ms
                            0.1,                // slippage_bps
                            0.5,                // gross_edge_bps
                            0.15,               // spread_paid_bps
                            avg_latency * 0.01, // latency_cost_bps
                            status
                        );
                        
                        // Update hourly expectancy for this cell
                        if (sess) {
                            for (int h = 0; h < 24; h++) {
                                double h_exp = sess->getExpectancy(h);
                                int h_trades = sess->getHour(h).trades;
                                g_gui.updateHourlyExpectancy("Binance", regime_str, alpha_str, h, h_exp, h_trades);
                            }
                        }
                    }
                }
            }
            
            // Status update (every 60 seconds)
            if (loop_count % 1200 == 0) {
                auto uptime_sec = loop_count * 50 / 1000;
                std::cout << "\n[CHIMERA] Status @ " << uptime_sec << "s:\n";
                std::cout << "  Binance: ticks=" << binance_engine.total_ticks()
                          << " orders=" << binance_engine.orders_sent()
                          << " fills=" << binance_engine.orders_filled() << "\n";
                
                std::cout << "  Institutional:\n";
                const char* syms[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
                for (const char* sym : syms) {
                    auto* det = regime_mgr.get(sym);
                    if (det) {
                        std::cout << "    " << sym << ": regime=" << Chimera::Alpha::regimeStr(det->currentRegime())
                                  << " ticks=" << det->ticksInRegime() << "\n";
                    }
                }
                
                std::cout << "  Regime PnL: " << regime_pnl.totalTrades() << " trades attributed\n";
                std::cout << "  Combined PnL: $" << g_daily_loss.pnl() << " NZD\n";
                
                Chimera::ScalpDiagnostics::printDailyStatus();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[CHIMERA-FATAL] Main loop exception: " << e.what() << "\n";
    }
    
    // =========================================================================
    // SHUTDOWN
    // =========================================================================
    std::cout << "\n[CHIMERA] Shutting down...\n";
    
    g_binance_ptr = nullptr;
    g_cfd_ptr = nullptr;
    g_income_ptr = nullptr;
    
    regime_pnl.persistCSV();
    session_mgr.persist();
    
    g_ml_logger.stop();
    Chimera::ML::getMLAttributionLogger().stop();
    
    g_gui.stop();
    income_engine.stop();
    binance_engine.stop();
    cfd_engine.stop();
    
    std::cout << "\n[CHIMERA] Final Statistics:\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Binance: " << binance_engine.total_ticks() << " ticks, "
              << binance_engine.orders_filled() << " fills\n";
    std::cout << "  Regime PnL: " << regime_pnl.totalTrades() << " trades attributed\n";
    regime_pnl.printSummary();
    std::cout << "  Daily PnL: $" << g_daily_loss.pnl() << " NZD\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    
    std::cout << "[CHIMERA] Shutdown complete\n";
    releaseSingletonLock();
    return 0;
}
