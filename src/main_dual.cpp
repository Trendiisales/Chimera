// =============================================================================
// main_dual.cpp - Chimera v4.1 - Dual Engine Entry Point
// =============================================================================
// ARCHITECTURE:
//   - Two completely independent engines
//   - BinanceEngine: CPU 1, Crypto via WebSocket
//   - CfdEngine: CPU 2, CFD/Forex via FIX 4.4
//   - They share NOTHING except GlobalKill and DailyLossGuard (atomics)
//   - GUIBroadcaster: WebSocket server for React dashboard (port 7777)
//
// v4.0 CHANGES:
//   - Pure HFT - no prediction, mechanical entry/exit
//   - Warmup = feed sanity only (not strategy confidence)
//   - AUTOMATIC SESSION LOGGING - ALL output saved to timestamped files
// =============================================================================
#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
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
            // Write to console
            if (console_buf_) {
                console_buf_->sputc(c);
            }
            // Write to file
            if (file_.is_open()) {
                file_.put(c);
                if (c == '\n') {
                    file_.flush();  // Flush on newlines for real-time logging
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
        // Create logs directory
        const char* log_dir = "logs";
        mkdir(log_dir, 0755);
        
        // Create timestamped log filename
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        std::ostringstream filename;
        filename << log_dir << "/chimera_"
                 << std::put_time(tm, "%Y%m%d_%H%M%S") << ".log";
        log_filename_ = filename.str();
        
        // Save original buffers
        original_cout_ = std::cout.rdbuf();
        original_cerr_ = std::cerr.rdbuf();
        
        // Create tee buffers
        cout_tee_ = std::make_unique<TeeStreambuf>(original_cout_, log_filename_);
        cerr_tee_ = std::make_unique<TeeStreambuf>(original_cerr_, log_filename_);
        
        if (!cout_tee_->is_open()) {
            return false;
        }
        
        // Redirect cout and cerr
        std::cout.rdbuf(cout_tee_.get());
        std::cerr.rdbuf(cerr_tee_.get());
        
        // Log header
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        std::cout << "  CHIMERA AUTO-LOG STARTED: " << log_filename_ << "\n";
        std::cout << "  Timestamp: " << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n\n";
        
        return true;
    }
    
    ~AutoLogger() {
        // Log footer
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        std::cout << "\n═══════════════════════════════════════════════════════════════\n";
        std::cout << "  SESSION ENDED: " << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "\n";
        std::cout << "  Log saved: " << log_filename_ << "\n";
        std::cout << "═══════════════════════════════════════════════════════════════\n";
        
        // Restore original buffers
        if (original_cout_) {
            std::cout.rdbuf(original_cout_);
        }
        if (original_cerr_) {
            std::cerr.rdbuf(original_cerr_);
        }
    }
    
    const std::string& getLogFilename() const { return log_filename_; }
};

// Global auto-logger instance
static AutoLogger g_auto_logger;

// =============================================================================
// SHARED STATE (Atomics Only)
// =============================================================================
// From shared includes - used by both engines
#include "core/GlobalKill.hpp"
#include "risk/DailyLossGuard.hpp"

// =============================================================================
// CRYPTO ENGINE (Binance)
// =============================================================================
#include "binance/BinanceEngine.hpp"
#include "binance/BinanceConfig.hpp"  // v7.13: For trade mode banner

// =============================================================================
// CFD ENGINE (cTrader)
// =============================================================================
// CfdEngine is in Omega namespace, uses its own GlobalKillSwitch
#include "CfdEngine.hpp"

// =============================================================================
// GUI BROADCASTER (WebSocket server for React dashboard)
// =============================================================================
#include "gui/GUIBroadcaster.hpp"
#include "SymbolEnabledManager.hpp"  // v4.3.3: Global symbol enable/disable

// =============================================================================
// GLOBAL STATE
// =============================================================================
std::atomic<bool> g_running{true};
std::atomic<int> g_signal_count{0};

// Chimera namespace globals (used by BinanceEngine)
Chimera::GlobalKill g_kill;
Chimera::DailyLossGuard g_daily_loss(-200.0);  // v4.5.1: Hard cap -$200 NZD (was -$500)

// Omega namespace globals (used by CfdEngine)
Omega::GlobalKillSwitch g_omega_kill;

// GUI Broadcaster (WebSocket server)
Chimera::GUIBroadcaster g_gui;

// Forward declarations for engines (needed for signal handler)
Chimera::Binance::BinanceEngine* g_binance_ptr = nullptr;
Omega::CfdEngine* g_cfd_ptr = nullptr;

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
        
        // Immediately stop engines to unblock SSL_read
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
// SINGLETON CHECK - v6.96: Ensure only one instance runs
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
    
    // Try to get exclusive lock (non-blocking)
    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        // Another instance is running - read its PID and kill it
        char buf[32] = {0};
        lseek(g_lock_fd, 0, SEEK_SET);
        ssize_t n = read(g_lock_fd, buf, sizeof(buf)-1);
        if (n > 0) {
            int old_pid = atoi(buf);
            if (old_pid > 0) {
                std::cout << "[CHIMERA] Killing existing instance (PID " << old_pid << ")...\n";
                kill(old_pid, SIGTERM);
                usleep(500000);  // 500ms grace
                kill(old_pid, SIGKILL);
                usleep(200000);  // 200ms for cleanup
            }
        }
        
        // Try lock again
        if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
            std::cerr << "[CHIMERA] ERROR: Cannot acquire lock - another instance may still be running\n";
            close(g_lock_fd);
            return false;
        }
    }
    
    // Write our PID to the lock file
    if (ftruncate(g_lock_fd, 0) < 0) {
        // Ignore truncate error - not critical
    }
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
    // v4.2.2: Force line-buffered stdout for real-time terminal output
    setvbuf(stdout, nullptr, _IOLBF, 0);
    
    // =========================================================================
    // AUTO-LOGGING: Initialize FIRST before any output
    // All stdout/stderr automatically saved to logs/chimera_YYYYMMDD_HHMMSS.log
    // =========================================================================
    if (!g_auto_logger.init()) {
        std::cerr << "[CHIMERA] WARNING: Auto-logging failed to initialize\n";
    }
    
    // v7.13: Print trade mode banner FIRST
    Chimera::Binance::print_trade_mode_banner();
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA - LIVE DATA + SHADOW MODE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Dashboard: http://YOUR_VPS_IP:8080/\n";
    std::cout << "  WebSocket: ws://YOUR_VPS_IP:7777\n";
    std::cout << "  CRYPTO: Binance LIVE (stream.binance.com) - SHADOW MODE\n";
    std::cout << "  CFD: EURUSD GBPUSD USDJPY AUDUSD USDCAD AUDNZD USDCHF\n";
    std::cout << "       XAUUSD XAGUSD | US30 NAS100 SPX500\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    // v6.96: Singleton check - kill old instance if running, acquire lock
    if (!acquireSingletonLock()) {
        std::cerr << "[CHIMERA] FATAL: Could not acquire singleton lock. Exiting.\n";
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);  // v6.95: Ignore SIGPIPE to prevent crash on client disconnect
#endif
    
    // =========================================================================
    // LOAD TRADING CONFIG FROM DISK
    // =========================================================================
    std::cout << "[CHIMERA] Loading trading config...\n";
    Chimera::getTradingConfig().loadFromFile("chimera_config.json");
    
    // =========================================================================
    // START GUI BROADCASTER
    // =========================================================================
    std::cout << "[CHIMERA] Starting GUI WebSocket server...\n";
    g_gui.initSymbols();  // Initialize symbol tracking
    g_gui.setKillSwitch(&g_kill);  // Connect kill switch to GUI
    g_gui.setVersion("v4.3.4");  // v4.3.4: STATE==RUNNING check - THE KEY FIX
    if (!g_gui.start()) {
        std::cerr << "[CHIMERA] WARNING: GUI server failed to start (continuing anyway)\n";
    } else {
        std::cout << "[CHIMERA] GUI server started on port 7777\n";
    }
    
    // v4.3.3: Confirm all symbols start DISABLED
    std::cout << "[CHIMERA] ═══════════════════════════════════════════════════════════\n";
    std::cout << "[CHIMERA] SYMBOL TRADING STATUS AT STARTUP:\n";
    std::cout << "[CHIMERA]   BTCUSDT: " << (Chimera::isSymbolTradingEnabled("BTCUSDT") ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "[CHIMERA]   ETHUSDT: " << (Chimera::isSymbolTradingEnabled("ETHUSDT") ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "[CHIMERA]   SOLUSDT: " << (Chimera::isSymbolTradingEnabled("SOLUSDT") ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "[CHIMERA] ALL SYMBOLS START DISABLED - Must click APPLY in GUI to enable!\n";
    std::cout << "[CHIMERA] ═══════════════════════════════════════════════════════════\n";
    
    // =========================================================================
    // CREATE BINANCE ENGINE (CPU 1)
    // =========================================================================
    std::cout << "[CHIMERA] Creating Binance Engine...\n";
    Chimera::Binance::BinanceEngine binance_engine(g_kill, g_daily_loss);
    g_binance_ptr = &binance_engine;  // For signal handler
    std::cout << "[CHIMERA] Binance Engine created\n";
    
    // =========================================================================
    // CREATE CFD ENGINE (CPU 2)
    // =========================================================================
    std::cout << "[CHIMERA] Creating CFD Engine...\n";
    Omega::CfdEngine cfd_engine;
    g_cfd_ptr = &cfd_engine;  // For signal handler
    
    // Configure FIX connection (BlackBull demo)
    // ALL settings loaded from config.ini - no hardcoded values
    Chimera::FIXConfig fix_config;
    
    cfd_engine.setFIXConfig(fix_config);
    cfd_engine.setKillSwitch(&g_omega_kill);
    
    // Configure symbols - add all the ones we want to track
    cfd_engine.setForexSymbols({"EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCAD", "AUDNZD", "USDCHF"});
    cfd_engine.setMetalsSymbols({"XAUUSD", "XAGUSD"});
    cfd_engine.setIndicesSymbols({"US30", "NAS100", "SPX500"});  // v6.83: Added indices!
    // v6.97: Removed duplicate setIndicesSymbols call that was here
    
    // Set order callback for PnL tracking AND GUI broadcast
    // v6.80: Added PnL parameter to callback for session tracking
    cfd_engine.setOrderCallback([](const char* symbol, int8_t side, double qty, double price, double pnl) {
        std::cout << "[CFD] Order: " << symbol 
                  << " side=" << (side > 0 ? "BUY" : "SELL")
                  << " qty=" << qty 
                  << " price=" << price;
        if (pnl != 0.0) std::cout << " pnl=" << pnl;
        std::cout << "\n";
        // Broadcast to GUI with actual price and PnL
        g_gui.broadcastTrade(symbol, side > 0 ? "BUY" : "SELL", qty, price, pnl);
    });
    
    // Set PnL callback to track daily P&L (NEW v6.79)
    // v6.97 FIX: Proper PnL conversion from bps to NZD
    // pnl_bps is basis points of the position value
    // For XAUUSD: 1 bps on 0.01 lot (1 oz) at $2600 = $2600 * 0.0001 = $0.26 USD
    // Convert to NZD using approximate rate (1.65 NZD/USD)
    cfd_engine.setPnLCallback([](const char* symbol, double pnl_bps, bool is_close) {
        if (is_close) {
            // Get symbol metadata for contract size
            std::string sym(symbol);
            double position_value = 0.01;  // Default to forex 0.01 lot
            double contract_multiplier = 100000.0;  // Forex standard lot
            
            if (sym == "XAUUSD" || sym == "GOLD") {
                // Gold: 0.01 lot = 1 oz, current price ~$2600
                contract_multiplier = 100.0;  // 1 lot = 100 oz
                position_value = 2600.0;      // Approximate price per oz
            } else if (sym == "XAGUSD") {
                // Silver: 1 lot = 5000 oz
                contract_multiplier = 5000.0;
                position_value = 30.0;        // Approximate price per oz
            } else if (sym == "US30" || sym == "NAS100" || sym == "SPX500") {
                // Indices: 1 lot = $1 per point
                contract_multiplier = 1.0;
                position_value = 1.0;
            } else {
                // Forex pairs: 0.01 lot = 1000 units
                contract_multiplier = 100000.0;
                position_value = 1.0;         // For USD pairs
            }
            
            // Calculate PnL: bps * position_value * 0.0001 * lot_size * contract
            // Simplified: use 0.01 lot as base, multiply by contract value
            double lot_size = 0.01;
            double base_value = lot_size * contract_multiplier * position_value;
            double pnl_usd = pnl_bps * base_value * 0.0001;
            
            // Convert USD to NZD (approximate rate)
            double usd_to_nzd = 1.65;
            double pnl_nzd = pnl_usd * usd_to_nzd;
            
            g_daily_loss.on_fill(pnl_nzd);
            std::cout << "[PNL] " << symbol << " closed: " << pnl_bps << " bps -> $" 
                      << pnl_usd << " USD -> $" << pnl_nzd << " NZD (total: $" 
                      << g_daily_loss.pnl() << ")\n";
        }
    });
    
    // Set tick callback to broadcast CFD prices to GUI
    cfd_engine.setTickCallback([](const char* symbol, double bid, double ask,
                                   double ofi, double vpin, double pressure, double latency_ms) {
        g_gui.updateMicro(ofi, vpin, pressure, ask - bid, bid, ask, symbol);
        g_gui.updateSymbolTick(symbol, bid, ask, latency_ms);
    });
    
    // Set market state callback to broadcast state to GUI (NEW v6.63)
    cfd_engine.setMarketStateCallback([](Chimera::MarketState state, Chimera::TradeIntent intent,
                                          int conviction, const char* reason) {
        g_gui.updateMarketState(state, intent, conviction, reason);
    });
    
    // Set bucket callback to broadcast current tick votes to GUI (NEW v6.68)
    cfd_engine.setBucketCallback([](int buy_votes, int sell_votes, int8_t consensus, 
                                    bool vetoed, const char* veto_reason) {
        g_gui.updateBuckets(buy_votes, sell_votes, consensus, vetoed, veto_reason);
    });
    
    std::cout << "[CHIMERA] CFD Engine created\n";
    
    // v4.2.2: Set shadow trade callback to broadcast crypto paper trades to GUI
    // Also log to CSV for post-session analysis
    static std::ofstream shadow_trade_log;
    static bool shadow_log_initialized = false;
    
    binance_engine.setShadowTradeCallback([](const char* symbol, int8_t side, double qty, double price, double pnl_bps) {
        // Initialize CSV log on first trade
        if (!shadow_log_initialized) {
            // Create logs directory if needed
            mkdir("logs", 0755);
            
            // Generate timestamped filename
            auto now = std::chrono::system_clock::now();
            std::time_t now_t = std::chrono::system_clock::to_time_t(now);
            char filename[64];
            std::strftime(filename, sizeof(filename), "logs/shadow_trades_%Y%m%d_%H%M%S.csv", std::localtime(&now_t));
            
            shadow_trade_log.open(filename);
            if (shadow_trade_log.is_open()) {
                shadow_trade_log << "timestamp,symbol,side,qty,price,pnl_bps,pnl_usd\n";
                std::cout << "[CHIMERA] Shadow trade log: " << filename << "\n";
            }
            shadow_log_initialized = true;
        }
        
        // side > 0 = entry/long, side < 0 = exit/short (or closing)
        const char* side_str = (side > 0) ? "BUY" : "SELL";
        
        // Convert pnl_bps to approximate USD (using BTC ~$90k, 0.001 BTC position)
        // 1 bps = 0.0001, so 5 bps on $90 position = $0.045
        double pnl_usd = (pnl_bps / 10000.0) * price * qty;
        
        std::cout << "[CRYPTO-SHADOW] " << symbol << " " << side_str 
                  << " qty=" << qty << " price=" << price;
        if (pnl_bps != 0.0) std::cout << " pnl=" << pnl_bps << "bps ($" << pnl_usd << ")";
        std::cout << "\n";
        
        // Log to CSV
        if (shadow_trade_log.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            shadow_trade_log << now_ms << "," << symbol << "," << side_str << ","
                            << std::fixed << std::setprecision(6) << qty << ","
                            << std::setprecision(2) << price << ","
                            << std::setprecision(2) << pnl_bps << ","
                            << std::setprecision(4) << pnl_usd << "\n";
            shadow_trade_log.flush();
        }
        
        // Broadcast to GUI (send USD, not bps)
        g_gui.broadcastTrade(symbol, side_str, qty, price, pnl_usd);
    });
    
    // =========================================================================
    // START ENGINES
    // =========================================================================
    std::cout << "\n[CHIMERA] Starting Binance Engine...\n";
    bool binance_ok = binance_engine.start();
    if (!binance_ok) {
        std::cout << "[CHIMERA] WARNING: Binance Engine failed to start (will retry)\n";
    } else {
        std::cout << "[CHIMERA] Binance Engine started\n";
    }
    
    std::cout << "[CHIMERA] Starting CFD Engine...\n";
    bool cfd_ok = cfd_engine.start();
    if (!cfd_ok) {
        std::cout << "[CHIMERA] WARNING: CFD Engine failed to start (will retry)\n";
    } else {
        std::cout << "[CHIMERA] CFD Engine started\n";
    }
    
    // Update GUI with initial connection status
    g_gui.updateConnections(binance_ok, cfd_ok);
    
    // v4.2.2: Get actual config to show correct environment
    const auto binance_cfg = Chimera::Binance::get_config();
    const char* binance_env = binance_cfg.is_testnet ? "TESTNET" : "LIVE";
    
    std::cout << "\n═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA RUNNING\n";
    std::cout << "  Binance: " << (binance_ok ? "ACTIVE" : "CONNECTING") << " (" << binance_env << ")\n";
    std::cout << "  cTrader: " << (cfd_ok ? "ACTIVE" : "CONNECTING") << "\n";
    std::cout << "  GUI: ws://localhost:7777 (Dashboard)\n";
    std::cout << "  HTTP: http://localhost:8080 (Dashboard HTML)\n";
    std::cout << "  Press Ctrl+C to exit\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n" << std::endl;
    
    // =========================================================================
    // MAIN LOOP - Monitor both engines + broadcast to GUI
    // =========================================================================
    uint64_t loop_count = 0;
    auto loop_start = std::chrono::steady_clock::now();
    
    // Track last crypto tick broadcast to avoid flooding GUI
    uint64_t last_crypto_broadcast_ms = 0;
    constexpr uint64_t CRYPTO_BROADCAST_INTERVAL_MS = 100;  // 10 updates/sec max
    
    std::cout << "[CHIMERA-DBG] Entering main loop...\n";
    
    try {
        while (g_running && !g_kill.killed()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 50ms for faster shutdown
            
            ++loop_count;
            
            // Calculate loop timing
            auto now = std::chrono::steady_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(now - loop_start).count();
            loop_start = now;
            
            // Get current time in ms for crypto broadcast throttling
            uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            // Get CFD stats safely
            const auto& cfd_stats = cfd_engine.getStats();
            
            // Update GUI with current state
            try {
                g_gui.updateHeartbeat(loop_count, elapsed_ms, 0.0);
                
                g_gui.updateOrderflow(
                    binance_engine.total_ticks() + cfd_stats.ticks_processed.load(),
                    binance_engine.orders_sent() + cfd_stats.orders_sent.load(),
                    binance_engine.orders_filled() + cfd_stats.orders_filled.load(),
                    0,  // rejects
                    static_cast<uint64_t>(cfd_stats.avgLatencyUs() * 1000)  // Convert μs to ns
                );
                
                // Update risk with drawdown
                double dd_used = g_daily_loss.drawdown_used();
                g_gui.updateRisk(
                    g_daily_loss.pnl(),
                    dd_used * 100.0,  // drawdown as percentage
                    0.0,  // global exposure
                    0     // positions
                );
                g_gui.updateDrawdownUsed(dd_used);
                
                g_gui.updateConnections(
                    binance_engine.is_running(),
                    cfd_engine.isConnected()
                );
                
                // Update state_gated count for GUI
                g_gui.updateStateGated(cfd_stats.state_gated.load());
                
                // =============================================================
                // BINANCE CRYPTO TICK BROADCAST (v6.95 - enhanced debug)
                // =============================================================
                // v6.95: Added debug output to trace crypto data flow
                static uint64_t crypto_debug_counter = 0;
                crypto_debug_counter++;
                
                // v3.6-clean: Removed verbose CRYPTO-STATE logging
                
                if (binance_engine.is_running() && 
                    (now_ms - last_crypto_broadcast_ms) >= CRYPTO_BROADCAST_INTERVAL_MS) {
                    
                    last_crypto_broadcast_ms = now_ms;
                    
                    // Broadcast BTCUSDT
                    uint16_t btc_id = Chimera::Binance::symbol_to_id("BTCUSDT", 7);
                    const auto* btc = binance_engine.get_symbol_thread(btc_id);
                    
                    // v3.6-clean: Removed verbose CRYPTO-DEBUG logging
                    
                    if (btc) {
                        const auto& book = btc->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("BTCUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                    
                    // Broadcast ETHUSDT
                    const auto* eth = binance_engine.get_symbol_thread(
                        Chimera::Binance::symbol_to_id("ETHUSDT", 7));
                    if (eth && eth->tick_count() > 0) {
                        const auto& book = eth->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("ETHUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                    
                    // Broadcast SOLUSDT
                    const auto* sol = binance_engine.get_symbol_thread(
                        Chimera::Binance::symbol_to_id("SOLUSDT", 7));
                    if (sol && sol->tick_count() > 0) {
                        const auto& book = sol->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("SOLUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                    
                    // Broadcast AVAXUSDT
                    const auto* avax = binance_engine.get_symbol_thread(
                        Chimera::Binance::symbol_to_id("AVAXUSDT", 8));
                    if (avax && avax->tick_count() > 0) {
                        const auto& book = avax->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("AVAXUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                    
                    // Broadcast LINKUSDT
                    const auto* link = binance_engine.get_symbol_thread(
                        Chimera::Binance::symbol_to_id("LINKUSDT", 8));
                    if (link && link->tick_count() > 0) {
                        const auto& book = link->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("LINKUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                    
                    // Broadcast OPUSDT
                    const auto* op = binance_engine.get_symbol_thread(
                        Chimera::Binance::symbol_to_id("OPUSDT", 6));
                    if (op && op->tick_count() > 0) {
                        const auto& book = op->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("OPUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                    
                    // Broadcast ARBUSDT
                    const auto* arb = binance_engine.get_symbol_thread(
                        Chimera::Binance::symbol_to_id("ARBUSDT", 7));
                    if (arb && arb->tick_count() > 0) {
                        const auto& book = arb->book();
                        double bid = book.best_bid();
                        double ask = book.best_ask();
                        if (bid > 0 && ask > 0) {
                            g_gui.updateSymbolTick("ARBUSDT", bid, ask, binance_engine.avg_latency_ms());
                        }
                    }
                }
                
                // Update latency stats from tracker
                // CFD stats track latency per tick in nanoseconds
                uint64_t ticks = cfd_stats.ticks_processed.load();
                uint64_t total_ns = cfd_stats.total_latency_ns.load();
                uint64_t max_ns = cfd_stats.max_latency_ns.load();
                
                // Calculate average
                uint64_t avg_ns = (ticks > 0) ? (total_ns / ticks) : 0;
                
                // v3.6-clean: Removed LAT-DBG logging
                
                // Use max as estimate for p99 (conservative)
                g_gui.updateLatencyStats(
                    avg_ns,   // avg in ns
                    avg_ns,   // min (use avg as estimate) 
                    max_ns,   // max
                    avg_ns,   // p50 (use avg as estimate)
                    max_ns    // p99 (use max as estimate)
                );
                
                // Also update pipeline latency (estimates)
                if (avg_ns > 0) {
                    g_gui.updatePipelineLatency(
                        avg_ns * 30 / 100,  // tick_to_signal ~30%
                        avg_ns * 20 / 100,  // signal_to_order ~20%
                        avg_ns * 50 / 100   // order_to_ack ~50%
                    );
                }
                
                // Get quality factors from CFD engine (if available)
                // For now, use reasonable defaults based on engine state
                double Q_vol = 1.0, Q_spr = 1.0, Q_liq = 1.0, Q_lat = 1.0, Q_dd = 1.0, corr_p = 1.0;
                
                // Q_dd from drawdown
                Q_dd = g_daily_loss.throttle_factor(2.0);
                
                // Q_lat from latency (if > 100μs, start reducing)
                double avg_lat_us = cfd_stats.avgLatencyUs();
                if (avg_lat_us > 50.0) {
                    Q_lat = 1.0 / (1.0 + 3.0 * std::max(0.0, (avg_lat_us / 50.0) - 1.0));
                }
                
                g_gui.updateQualityFactors(Q_vol, Q_spr, Q_liq, Q_lat, Q_dd, corr_p);
                
                // Regime state (get UTC hour)
                auto now_t = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now_t);
                struct tm* utc_tm = gmtime(&time_t_now);
                int utc_hour = utc_tm ? utc_tm->tm_hour : 12;
                
                g_gui.updateRegime(
                    1.0,    // vol_z
                    1.0,    // spread_z
                    1.0,    // liq_z
                    avg_lat_us / 50.0,  // lat_z (normalized to 50μs baseline)
                    false,  // is_trending
                    false,  // is_volatile
                    utc_hour
                );
                
                // v3.0: Update Expectancy Health Panel
                // Determine session from UTC hour
                const char* session = "O";  // Overnight default
                if (utc_hour >= 0 && utc_hour < 8) session = "A";       // Asia
                else if (utc_hour >= 8 && utc_hour < 16) session = "L"; // London
                else if (utc_hour >= 13 && utc_hour < 21) session = "N"; // NY (overlaps with London)
                
                // Update crypto symbols with REAL data from symbol threads
                const char* crypto_syms[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
                for (const char* sym : crypto_syms) {
                    uint16_t sym_id = Chimera::Binance::symbol_to_id(sym, strlen(sym));
                    const auto* thread = binance_engine.get_symbol_thread(sym_id);
                    if (thread && thread->tick_count() > 0) {
                        // Get real expectancy data from the symbol thread
                        double expectancy = thread->expectancy_authority_fast();
                        int trades = thread->expectancy_trades();
                        const char* regime_str = thread->regime_str();
                        const char* block_reason = thread->last_block_reason_str();
                        const char* state = (trades >= 5) ? "SHADOW" : "WARMUP";
                        
                        g_gui.updateExpectancyHealth(
                            sym,
                            regime_str,         // Real regime from thread
                            expectancy,         // Real expectancy from thread
                            0.0,                // slope - display only, not tracked
                            0.0,                // slope_delta - display only, not tracked
                            0.0,                // divergence_bps - N/A for shadow
                            0,                  // divergence_streak - N/A for shadow
                            session,
                            state,              // WARMUP until enough trades
                            block_reason        // v4.2.2: Actual block reason from crypto gate
                        );
                    }
                }
                
                // Update CFD symbols with REAL data from shadow state
                const char* cfd_syms[] = {"XAUUSD", "EURUSD", "GBPUSD", "USDJPY", "NAS100", "SPX500", "XAGUSD"};
                for (const char* sym : cfd_syms) {
                    if (cfd_engine.isConnected()) {
                        double expectancy = cfd_engine.getShadowExpectancy(sym);
                        int trades = cfd_engine.getShadowTrades(sym);
                        int tier = cfd_engine.getSymbolTier(sym);
                        
                        // Determine state based on tier and trades
                        const char* state = "OFF";
                        if (tier == 4) state = "OFF";
                        else if (trades < 5) state = "WARMUP";
                        else state = "SHADOW";
                        
                        g_gui.updateExpectancyHealth(
                            sym,
                            "CLEAN",        // regime - display only
                            expectancy,     // Real expectancy from shadow
                            0.0,            // slope - display only, not tracked
                            0.0,            // slope_delta - display only, not tracked
                            0.0,            // divergence_bps - N/A
                            0,              // divergence_streak - N/A
                            session,
                            state,
                            ""              // pause_reason
                        );
                    }
                }
                
                // Note: Bucket votes now sent per-tick via bucketCallback
                
            } catch (const std::exception& e) {
                std::cerr << "[CHIMERA-ERR] GUI update exception: " << e.what() << "\n";
            }
            
            // v3.6-clean: Status update every 60 seconds (was 10s)
            if (loop_count % 1200 == 0) {  // 1200 * 50ms = 60 seconds
                auto uptime_sec = loop_count * 50 / 1000;
                std::cout << "[CHIMERA] Status @ " << uptime_sec << "s:\n";
                std::cout << "  Binance: ticks=" << binance_engine.total_ticks()
                          << " orders=" << binance_engine.orders_sent()
                          << " fills=" << binance_engine.orders_filled() << "\n";
                std::cout << "  cTrader: ticks=" << cfd_stats.ticks_processed.load()
                          << " orders=" << cfd_stats.orders_sent.load()
                          << " fills=" << cfd_stats.orders_filled.load()
                          << " latency=" << cfd_stats.avgLatencyUs() << "μs"
                          << " state_gated=" << cfd_stats.state_gated.load() << "\n";
                std::cout << "  Combined PnL: $" << g_daily_loss.pnl() << " NZD\n";
                std::cout << "  GUI clients: " << g_gui.clientCount() << std::endl;  // v4.2.2: Force flush
                
                // v7.08: Print per-symbol FIX tick stats
                cfd_engine.printSymbolTickStats();
                
                // Check daily loss limit
                if (!g_daily_loss.allow()) {
                    std::cout << "[CHIMERA] DAILY LOSS LIMIT HIT - Stopping trading\n";
                    g_kill.kill();
                    g_omega_kill.triggerAll();
                    break;
                }
                
                // Check if CFD engine died unexpectedly
                if (!cfd_engine.isRunning() && cfd_ok) {
                    std::cerr << "[CHIMERA-WARN] CFD engine stopped unexpectedly!\n";
                }
                
                // Check if Binance engine died unexpectedly
                if (!binance_engine.is_running() && binance_ok) {
                    std::cerr << "[CHIMERA-WARN] Binance engine stopped unexpectedly!\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[CHIMERA-FATAL] Main loop exception: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[CHIMERA-FATAL] Main loop unknown exception!\n";
    }
    
    std::cout << "[CHIMERA-DBG] Main loop exited. g_running=" << g_running.load() 
              << " g_kill=" << g_kill.killed() << "\n";
    
    // =========================================================================
    // SHUTDOWN
    // =========================================================================
    std::cout << "\n[CHIMERA] Main loop exited, finalizing shutdown...\n";
    
    // Clear global pointers first to prevent signal handler from re-calling stop
    g_binance_ptr = nullptr;
    g_cfd_ptr = nullptr;
    
    // Stop GUI broadcaster
    g_gui.stop();
    
    // Stop engines if not already stopped by signal handler
    binance_engine.stop();
    cfd_engine.stop();
    
    // Final stats
    const auto& cfd_stats = cfd_engine.getStats();
    
    std::cout << "\n[CHIMERA] Final Statistics:\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  BINANCE ENGINE:\n";
    std::cout << "    Ticks processed: " << binance_engine.total_ticks() << "\n";
    std::cout << "    Orders sent:     " << binance_engine.orders_sent() << "\n";
    std::cout << "    Orders filled:   " << binance_engine.orders_filled() << "\n";
    std::cout << "  CTRADER ENGINE:\n";
    std::cout << "    Ticks processed: " << cfd_stats.ticks_processed.load() << "\n";
    std::cout << "    FIX messages:    " << cfd_stats.fix_messages.load() << "\n";
    std::cout << "    Orders sent:     " << cfd_stats.orders_sent.load() << "\n";
    std::cout << "    Orders filled:   " << cfd_stats.orders_filled.load() << "\n";
    std::cout << "    Avg latency:     " << cfd_stats.avgLatencyUs() << " μs\n";
    std::cout << "    Max latency:     " << (cfd_stats.max_latency_ns.load() / 1000.0) << " μs\n";
    std::cout << "    Buy votes:       " << cfd_stats.buy_votes.load() << "\n";
    std::cout << "    Sell votes:      " << cfd_stats.sell_votes.load() << "\n";
    std::cout << "    Consensus trades:" << cfd_stats.consensus_trades.load() << "\n";
    std::cout << "    State gated:     " << cfd_stats.state_gated.load() << "\n";
    std::cout << "  COMBINED:\n";
    std::cout << "    Daily PnL:       $" << g_daily_loss.pnl() << " NZD\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    
    std::cout << "\n[CHIMERA] Shutdown complete\n";
    releaseSingletonLock();
    return 0;
}
