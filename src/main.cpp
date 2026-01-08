// =============================================================================
// main.cpp - Chimera v4.14.2 Unified Entry Point
// =============================================================================
// SYMBOLS: XAUUSD, NAS100, US30 ONLY
// ARCHITECTURE: Clean CRTP-based engines
// PROTOCOL: cTrader OpenAPI (Protobuf over SSL)
// GUI: Embedded WebSocket + HTTP server
//
// NO CRYPTO - NO FOREX - THREE SYMBOLS ONLY
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
#include <unordered_map>
#include <memory>

#ifdef __linux__
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#endif

// =============================================================================
// UNIFIED ENGINE
// =============================================================================
#include "engines/ChimeraUnifiedEngine.hpp"

// =============================================================================
// CTRADER OPENAPI CLIENT
// =============================================================================
#include "openapi/CTraderOpenAPIClient.hpp"

// =============================================================================
// GUI BROADCASTER (WebSocket + HTTP)
// =============================================================================
#include "gui/GUIBroadcaster.hpp"

// =============================================================================
// VERSION
// =============================================================================
static constexpr const char* CHIMERA_VERSION = "v4.14.2";

// =============================================================================
// LOCKED SYMBOLS - DO NOT MODIFY
// =============================================================================
static const std::vector<std::string> ENABLED_SYMBOLS = {
    "XAUUSD",
    "NAS100",
    "US30"
};

// =============================================================================
// RISK PARAMETERS (LOCKED)
// =============================================================================
namespace RiskParams {
    // HARDCODED HARD STOP - DO NOT CHANGE WITHOUT JO'S APPROVAL
    static constexpr double DAILY_LOSS_LIMIT   = -200.0;  // NZD - ABSOLUTE HARD STOP
    static constexpr double MAX_POSITION_SIZE  = 1.0;     // lots
    static constexpr int    MAX_TRADES_PER_DAY = 5;       // per symbol
    static constexpr double MAX_GLOBAL_EXPOSURE = 3.0;    // lots total across all symbols
    
    // Per-symbol risk allocation
    static constexpr double XAUUSD_RISK_PCT = 0.50;  // 50% of daily risk
    static constexpr double NAS100_RISK_PCT = 0.30;  // 30%
    static constexpr double US30_RISK_PCT   = 0.20;  // 20%
}

// =============================================================================
// GLOBAL STATE
// =============================================================================
static std::atomic<bool> g_running{true};
static std::atomic<int>  g_signal_count{0};
static std::atomic<double> g_daily_pnl{0.0};
static std::atomic<int>  g_daily_trades{0};

// Per-symbol trade counts
static std::atomic<int> g_xauusd_trades{0};
static std::atomic<int> g_nas100_trades{0};
static std::atomic<int> g_us30_trades{0};

// Per-symbol positions (lots) - for exposure tracking
static std::atomic<double> g_xauusd_position{0.0};
static std::atomic<double> g_nas100_position{0.0};
static std::atomic<double> g_us30_position{0.0};

// Per-symbol latest tick prices (for GUI)
static std::atomic<double> g_xauusd_bid{0.0};
static std::atomic<double> g_xauusd_ask{0.0};
static std::atomic<double> g_nas100_bid{0.0};
static std::atomic<double> g_nas100_ask{0.0};
static std::atomic<double> g_us30_bid{0.0};
static std::atomic<double> g_us30_ask{0.0};

// Tick counters
static std::atomic<uint64_t> g_total_ticks{0};
static std::atomic<uint64_t> g_xauusd_ticks{0};
static std::atomic<uint64_t> g_nas100_ticks{0};
static std::atomic<uint64_t> g_us30_ticks{0};

// =============================================================================
// CONFIG LOADER
// =============================================================================
class ConfigLoader {
public:
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[CONFIG] Failed to open: " << path << "\n";
            return false;
        }
        
        std::string line, section;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    section = line.substr(1, end - 1);
                }
                continue;
            }
            
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
                
                values_[section + "." + key] = value;
            }
        }
        
        std::cout << "[CONFIG] Loaded " << values_.size() << " settings from " << path << "\n";
        return true;
    }
    
    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second : def;
    }
    
    int64_t getInt(const std::string& key, int64_t def = 0) const {
        auto it = values_.find(key);
        if (it == values_.end()) return def;
        try { return std::stoll(it->second); }
        catch (...) { return def; }
    }
    
    double getDouble(const std::string& key, double def = 0.0) const {
        auto it = values_.find(key);
        if (it == values_.end()) return def;
        try { return std::stod(it->second); }
        catch (...) { return def; }
    }
    
    bool getBool(const std::string& key, bool def = false) const {
        auto it = values_.find(key);
        if (it == values_.end()) return def;
        return (it->second == "true" || it->second == "1" || it->second == "yes");
    }

private:
    std::unordered_map<std::string, std::string> values_;
};

static ConfigLoader g_config;

// =============================================================================
// AUTO-LOGGING SYSTEM
// =============================================================================
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
        if (file_.is_open()) {
            file_.close();
        }
    }
    
protected:
    int overflow(int c) override {
        if (c == EOF) return c;
        
        if (console_buf_) console_buf_->sputc(static_cast<char>(c));
        if (file_.is_open()) file_.put(static_cast<char>(c));
        
        return c;
    }
    
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (console_buf_) console_buf_->sputn(s, n);
        if (file_.is_open()) file_.write(s, n);
        return n;
    }
    
    int sync() override {
        if (console_buf_) console_buf_->pubsync();
        if (file_.is_open()) file_.flush();
        return 0;
    }
};

// =============================================================================
// LOGGING SETUP
// =============================================================================
static std::unique_ptr<TeeStreambuf> g_tee_cout;
static std::unique_ptr<TeeStreambuf> g_tee_cerr;
static std::streambuf* g_orig_cout = nullptr;
static std::streambuf* g_orig_cerr = nullptr;

void setupLogging() {
    // Create log filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);
    
    char filename[256];
    std::snprintf(filename, sizeof(filename), 
        "chimera_%04d%02d%02d_%02d%02d%02d.log",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    
    // Save original buffers
    g_orig_cout = std::cout.rdbuf();
    g_orig_cerr = std::cerr.rdbuf();
    
    // Create tee buffers
    g_tee_cout = std::make_unique<TeeStreambuf>(g_orig_cout, filename);
    g_tee_cerr = std::make_unique<TeeStreambuf>(g_orig_cerr, filename);
    
    // Redirect streams
    std::cout.rdbuf(g_tee_cout.get());
    std::cerr.rdbuf(g_tee_cerr.get());
    
    std::cout << "[LOG] Logging to " << filename << "\n";
}

void teardownLogging() {
    if (g_orig_cout) std::cout.rdbuf(g_orig_cout);
    if (g_orig_cerr) std::cerr.rdbuf(g_orig_cerr);
    g_tee_cout.reset();
    g_tee_cerr.reset();
}

// =============================================================================
// SIGNAL HANDLER
// =============================================================================
void signalHandler(int signum) {
    int count = g_signal_count.fetch_add(1) + 1;
    
    std::cout << "\n[SIGNAL] Received signal " << signum << " (count=" << count << ")\n";
    
    if (count >= 2) {
        std::cout << "[SIGNAL] Force exit on second signal\n";
        std::_Exit(1);
    }
    
    g_running.store(false);
}

// =============================================================================
// TIMESTAMP HELPERS
// =============================================================================
static inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm = *std::localtime(&time);
    
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
        tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

static inline uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// =============================================================================
// RISK GOVERNOR
// =============================================================================
class RiskGovernor {
public:
    bool canTrade(const std::string& symbol) const {
        // Check daily loss limit
        if (g_daily_pnl.load() <= RiskParams::DAILY_LOSS_LIMIT) {
            std::cout << "[RISK] Daily loss limit hit: $" << g_daily_pnl.load() << "\n";
            return false;
        }
        
        // Check per-symbol trade limit
        if (symbol == "XAUUSD" && g_xauusd_trades.load() >= RiskParams::MAX_TRADES_PER_DAY) {
            return false;
        }
        if (symbol == "NAS100" && g_nas100_trades.load() >= RiskParams::MAX_TRADES_PER_DAY) {
            return false;
        }
        if (symbol == "US30" && g_us30_trades.load() >= RiskParams::MAX_TRADES_PER_DAY) {
            return false;
        }
        
        // Check global exposure
        double total_exposure = std::abs(g_xauusd_position.load()) + 
                               std::abs(g_nas100_position.load()) + 
                               std::abs(g_us30_position.load());
        if (total_exposure >= RiskParams::MAX_GLOBAL_EXPOSURE) {
            std::cout << "[RISK] Global exposure limit: " << total_exposure << " lots\n";
            return false;
        }
        
        return true;
    }
    
    double getPositionSize(const std::string& symbol, double signal_strength) const {
        // Base size capped at MAX_POSITION_SIZE
        double base_size = std::min(signal_strength, RiskParams::MAX_POSITION_SIZE);
        
        // Scale by risk allocation
        if (symbol == "XAUUSD") {
            return base_size * RiskParams::XAUUSD_RISK_PCT;
        } else if (symbol == "NAS100") {
            return base_size * RiskParams::NAS100_RISK_PCT;
        } else if (symbol == "US30") {
            return base_size * RiskParams::US30_RISK_PCT;
        }
        
        return 0.01;  // Minimum
    }
    
    void recordTrade(const std::string& symbol) {
        g_daily_trades.fetch_add(1);
        if (symbol == "XAUUSD") g_xauusd_trades.fetch_add(1);
        else if (symbol == "NAS100") g_nas100_trades.fetch_add(1);
        else if (symbol == "US30") g_us30_trades.fetch_add(1);
    }
    
    void updatePnL(double pnl_change) {
        double current = g_daily_pnl.load();
        while (!g_daily_pnl.compare_exchange_weak(current, current + pnl_change));
    }
    
    void resetDaily() {
        g_daily_pnl.store(0.0);
        g_daily_trades.store(0);
        g_xauusd_trades.store(0);
        g_nas100_trades.store(0);
        g_us30_trades.store(0);
        std::cout << "[RISK] Daily counters reset\n";
    }
};

static RiskGovernor g_risk;

// =============================================================================
// EXECUTION MANAGER
// =============================================================================
class ExecutionManager {
public:
    ExecutionManager(Chimera::CTraderOpenAPIClient& client) : client_(client) {}
    
    void executeSignal(const chimera::TradeSignal& signal) {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        
        // Risk check
        if (!g_risk.canTrade(signal.symbol)) {
            std::cout << "[EXEC] Signal blocked by risk: " << signal.symbol << "\n";
            return;
        }
        
        // Calculate position size
        double size = g_risk.getPositionSize(signal.symbol, signal.size_mult);
        
        // Determine side
        char side = (signal.direction > 0) ? Chimera::OrderSide::Buy : Chimera::OrderSide::Sell;
        
        std::cout << "[EXEC] " << timestamp() << " Executing: " 
                  << signal.symbol << " " << (side == Chimera::OrderSide::Buy ? "BUY" : "SELL")
                  << " " << std::fixed << std::setprecision(2) << size << " lots"
                  << " @ " << std::setprecision(5) << signal.price
                  << " reason=" << signal.reason << "\n";
        
        // Send order via OpenAPI
        bool sent = client_.sendMarketOrder(signal.symbol, side, size);
        
        if (sent) {
            g_risk.recordTrade(signal.symbol);
            pending_orders_++;
        } else {
            std::cerr << "[EXEC] Order send failed for " << signal.symbol << "\n";
        }
    }
    
    void onExecReport(const Chimera::CTraderExecReport& report) {
        std::lock_guard<std::mutex> lock(exec_mutex_);
        
        if (report.isFill()) {
            std::cout << "[EXEC] FILL: " << report.symbol 
                      << " " << report.lastQty << " @ " << report.lastPx << "\n";
            
            // Update positions
            double qty_signed = (report.side == Chimera::OrderSide::Buy) ? 
                               report.lastQty : -report.lastQty;
            
            if (report.symbol == "XAUUSD") {
                double pos = g_xauusd_position.load();
                g_xauusd_position.store(pos + qty_signed);
            } else if (report.symbol == "NAS100") {
                double pos = g_nas100_position.load();
                g_nas100_position.store(pos + qty_signed);
            } else if (report.symbol == "US30") {
                double pos = g_us30_position.load();
                g_us30_position.store(pos + qty_signed);
            }
            
            filled_orders_++;
        } else if (report.isReject()) {
            std::cerr << "[EXEC] REJECTED: " << report.clOrdID 
                      << " - " << report.text << "\n";
            rejected_orders_++;
        }
        
        if (pending_orders_ > 0) pending_orders_--;
    }
    
    uint64_t getPendingOrders() const { return pending_orders_.load(); }
    uint64_t getFilledOrders() const { return filled_orders_.load(); }
    uint64_t getRejectedOrders() const { return rejected_orders_.load(); }
    
private:
    Chimera::CTraderOpenAPIClient& client_;
    std::mutex exec_mutex_;
    std::atomic<uint64_t> pending_orders_{0};
    std::atomic<uint64_t> filled_orders_{0};
    std::atomic<uint64_t> rejected_orders_{0};
};

// =============================================================================
// PRINT BANNER
// =============================================================================
void printBanner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════════════════════╗
║                                                                           ║
║     ██████╗██╗  ██╗██╗███╗   ███╗███████╗██████╗  █████╗                  ║
║    ██╔════╝██║  ██║██║████╗ ████║██╔════╝██╔══██╗██╔══██╗                 ║
║    ██║     ███████║██║██╔████╔██║█████╗  ██████╔╝███████║                 ║
║    ██║     ██╔══██║██║██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║                 ║
║    ╚██████╗██║  ██║██║██║ ╚═╝ ██║███████╗██║  ██║██║  ██║                 ║
║     ╚═════╝╚═╝  ╚═╝╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝                 ║
║                                                                           ║
║         UNIFIED HFT ENGINE - XAUUSD | NAS100 | US30                       ║
║                                                                           ║
╠═══════════════════════════════════════════════════════════════════════════╣
║  Version: )" << CHIMERA_VERSION << R"(                                                         ║
║  Protocol: cTrader OpenAPI (SSL/TCP)                                      ║
║  Symbols: XAUUSD, NAS100, US30                                            ║
║  Risk: Daily=$200 NZD | MaxPos=1.0 | MaxTrades=5/symbol                   ║
╚═══════════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

// =============================================================================
// MAIN
// =============================================================================
int main(int argc, char* argv[]) {
    // Setup signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Setup logging
    setupLogging();
    
    // Print banner
    printBanner();
    
    std::cout << "[MAIN] " << timestamp() << " Chimera " << CHIMERA_VERSION << " starting...\n";
    
    // ==========================================================================
    // LOAD CONFIGURATION
    // ==========================================================================
    std::string config_path = "config.ini";
    if (argc > 1) {
        config_path = argv[1];
    }
    
    if (!g_config.load(config_path)) {
        std::cerr << "[MAIN] Failed to load config, using defaults\n";
    }
    
    // ==========================================================================
    // INITIALIZE OPENAPI CLIENT
    // ==========================================================================
    Chimera::CTraderOpenAPIClient openapi;
    
    Chimera::OpenAPIConfig api_config;
    api_config.clientId = g_config.getString("openapi.client_id", "");
    api_config.clientSecret = g_config.getString("openapi.client_secret", "");
    api_config.accessToken = g_config.getString("openapi.access_token", "");
    api_config.refreshToken = g_config.getString("openapi.refresh_token", "");
    api_config.accountId = static_cast<uint64_t>(g_config.getInt("openapi.account_id", 0));
    api_config.host = g_config.getString("openapi.host", "demo.ctraderapi.com");
    api_config.port = static_cast<int>(g_config.getInt("openapi.port", 5035));
    api_config.isLive = g_config.getBool("openapi.is_live", false);
    
    openapi.setConfig(api_config);
    
    std::cout << "[MAIN] OpenAPI config loaded:\n"
              << "  Host: " << api_config.host << ":" << api_config.port << "\n"
              << "  Account: " << api_config.accountId << "\n"
              << "  Mode: " << (api_config.isLive ? "LIVE" : "DEMO") << "\n";
    
    // ==========================================================================
    // INITIALIZE TRADING ENGINE
    // ==========================================================================
    chimera::ChimeraUnifiedSystem engine;
    
    // Create execution manager
    ExecutionManager exec_mgr(openapi);
    
    // Set signal callback
    engine.setCallback([&exec_mgr](const chimera::TradeSignal& signal) {
        exec_mgr.executeSignal(signal);
    });
    
    std::cout << "[MAIN] Trading engine initialized\n";
    
    // ==========================================================================
    // INITIALIZE GUI BROADCASTER
    // ==========================================================================
    Chimera::GUIBroadcaster gui;
    gui.setVersion(CHIMERA_VERSION);
    
    if (gui.start()) {
        std::cout << "[MAIN] GUI server started (WebSocket:7777 HTTP:8080)\n";
    } else {
        std::cerr << "[MAIN] WARNING: GUI server failed to start\n";
    }
    
    // ==========================================================================
    // SETUP TICK CALLBACK
    // ==========================================================================
    openapi.setOnTick([&engine, &gui](const Chimera::CTraderTick& tick) {
        g_total_ticks.fetch_add(1);
        uint64_t ts_ns = now_ns();
        
        // Route tick to appropriate engine
        if (tick.symbol == "XAUUSD") {
            g_xauusd_ticks.fetch_add(1);
            g_xauusd_bid.store(tick.bid);
            g_xauusd_ask.store(tick.ask);
            engine.onXAUUSDTick(tick.bid, tick.ask, ts_ns);
            
        } else if (tick.symbol == "NAS100") {
            g_nas100_ticks.fetch_add(1);
            g_nas100_bid.store(tick.bid);
            g_nas100_ask.store(tick.ask);
            engine.onNAS100Tick(tick.bid, tick.ask, ts_ns);
            
        } else if (tick.symbol == "US30") {
            g_us30_ticks.fetch_add(1);
            g_us30_bid.store(tick.bid);
            g_us30_ask.store(tick.ask);
            engine.onUS30Tick(tick.bid, tick.ask, ts_ns);
        }
        
        // Update GUI symbol data
        gui.updateTick(tick.symbol.c_str(), tick.bid, tick.ask, 0.0, 0.0, 0.0, tick.ask - tick.bid);
    });
    
    // ==========================================================================
    // SETUP EXECUTION CALLBACK
    // ==========================================================================
    openapi.setOnExec([&exec_mgr, &gui](const Chimera::CTraderExecReport& report) {
        exec_mgr.onExecReport(report);
        
        // Broadcast fill to GUI
        if (report.isFill()) {
            const char* side_str = (report.side == Chimera::OrderSide::Buy) ? "BUY" : "SELL";
            gui.broadcastTrade(report.symbol.c_str(), side_str,
                              report.lastQty, report.lastPx, 0.0);
        }
    });
    
    // ==========================================================================
    // CONNECT TO CTRADER
    // ==========================================================================
    std::cout << "[MAIN] Connecting to cTrader OpenAPI...\n";
    
    if (!openapi.connect()) {
        std::cerr << "[MAIN] Failed to connect to cTrader\n";
        gui.stop();
        teardownLogging();
        return 1;
    }
    
    std::cout << "[MAIN] Connected to cTrader OpenAPI\n";
    
    // Wait for security list
    std::cout << "[MAIN] Waiting for symbol list...\n";
    int wait_count = 0;
    while (!openapi.isSecurityListReady() && wait_count < 30 && g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_count++;
    }
    
    if (!openapi.isSecurityListReady()) {
        std::cerr << "[MAIN] Timeout waiting for symbol list\n";
        openapi.disconnect();
        gui.stop();
        teardownLogging();
        return 1;
    }
    
    std::cout << "[MAIN] Symbol list received\n";
    
    // ==========================================================================
    // SUBSCRIBE TO MARKET DATA
    // ==========================================================================
    std::cout << "[MAIN] Subscribing to market data...\n";
    
    for (const auto& symbol : ENABLED_SYMBOLS) {
        if (openapi.subscribeMarketData(symbol)) {
            std::cout << "[MAIN] Subscribed to " << symbol << "\n";
        } else {
            std::cerr << "[MAIN] Failed to subscribe to " << symbol << "\n";
        }
    }
    
    // ==========================================================================
    // MAIN LOOP
    // ==========================================================================
    std::cout << "\n[MAIN] ======== ENTERING MAIN LOOP ========\n";
    std::cout << "[MAIN] Press Ctrl+C to stop\n\n";
    
    auto start_time = std::chrono::steady_clock::now();
    auto last_status = start_time;
    auto last_daily_reset = std::chrono::system_clock::now();
    
    int status_interval_sec = 30;  // Print status every 30 seconds
    
    while (g_running.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // =======================================================================
        // PERIODIC STATUS PRINT
        // =======================================================================
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_status).count();
        if (elapsed >= status_interval_sec) {
            last_status = now;
            
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            std::cout << "\n[STATUS] " << timestamp() << " Uptime: " << uptime << "s\n"
                      << "  Ticks: XAUUSD=" << g_xauusd_ticks.load() 
                      << " NAS100=" << g_nas100_ticks.load()
                      << " US30=" << g_us30_ticks.load()
                      << " Total=" << g_total_ticks.load() << "\n"
                      << "  Prices: XAUUSD=" << std::fixed << std::setprecision(2) << g_xauusd_bid.load()
                      << "/" << g_xauusd_ask.load()
                      << " NAS100=" << std::setprecision(1) << g_nas100_bid.load() 
                      << "/" << g_nas100_ask.load()
                      << " US30=" << g_us30_bid.load() << "/" << g_us30_ask.load() << "\n"
                      << "  Trades: " << g_daily_trades.load() 
                      << " (XAUUSD=" << g_xauusd_trades.load()
                      << " NAS100=" << g_nas100_trades.load()
                      << " US30=" << g_us30_trades.load() << ")\n"
                      << "  Orders: Pending=" << exec_mgr.getPendingOrders()
                      << " Filled=" << exec_mgr.getFilledOrders()
                      << " Rejected=" << exec_mgr.getRejectedOrders() << "\n"
                      << "  Daily P&L: $" << std::setprecision(2) << g_daily_pnl.load() << "\n"
                      << "  Signals: " << engine.signals_total.load() << "\n";
            
            // Update GUI 
            gui.updateOrderflow(g_total_ticks.load(), 
                               exec_mgr.getFilledOrders() + exec_mgr.getRejectedOrders() + exec_mgr.getPendingOrders(),
                               exec_mgr.getFilledOrders(),
                               exec_mgr.getRejectedOrders(),
                               0);
            gui.updateRisk(g_daily_pnl.load(), 0.0, 0.0, 0);
            gui.updateConnections(openapi.isConnected());
        }
        
        // =======================================================================
        // DAILY RESET CHECK (UTC midnight)
        // =======================================================================
        auto now_sys = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now_sys);
        auto last_reset_time = std::chrono::system_clock::to_time_t(last_daily_reset);
        
        std::tm now_tm = *std::gmtime(&now_time);
        std::tm last_tm = *std::gmtime(&last_reset_time);
        
        if (now_tm.tm_yday != last_tm.tm_yday) {
            std::cout << "[MAIN] Daily reset triggered (UTC midnight)\n";
            g_risk.resetDaily();
            engine.resetDaily();
            last_daily_reset = now_sys;
        }
        
        // =======================================================================
        // CHECK CONNECTION
        // =======================================================================
        if (!openapi.isConnected()) {
            std::cerr << "[MAIN] Connection lost, attempting reconnect...\n";
            
            if (openapi.connect()) {
                // Re-subscribe
                for (const auto& symbol : ENABLED_SYMBOLS) {
                    openapi.subscribeMarketData(symbol);
                }
                std::cout << "[MAIN] Reconnected successfully\n";
            } else {
                std::cerr << "[MAIN] Reconnect failed, will retry...\n";
            }
        }
        
        // Sleep to prevent busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // ==========================================================================
    // SHUTDOWN
    // ==========================================================================
    std::cout << "\n[MAIN] ======== SHUTTING DOWN ========\n";
    
    // Print final stats
    engine.printStats();
    
    std::cout << "[MAIN] Final Stats:\n"
              << "  Total Ticks: " << g_total_ticks.load() << "\n"
              << "  Total Trades: " << g_daily_trades.load() << "\n"
              << "  Filled Orders: " << exec_mgr.getFilledOrders() << "\n"
              << "  Rejected Orders: " << exec_mgr.getRejectedOrders() << "\n"
              << "  Daily P&L: $" << std::fixed << std::setprecision(2) << g_daily_pnl.load() << "\n";
    
    // Disconnect
    std::cout << "[MAIN] Disconnecting from cTrader...\n";
    openapi.disconnect();
    
    // Stop GUI
    std::cout << "[MAIN] Stopping GUI server...\n";
    gui.stop();
    
    // Teardown logging
    std::cout << "[MAIN] Chimera " << CHIMERA_VERSION << " shutdown complete\n";
    teardownLogging();
    
    return 0;
}
