// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Main Engine v1.2.0
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
// CODENAME: APEX-ML
// ARCHITECTURE: Dual-engine (XAUUSD + NAS100) with integrated GUI/ML/Logging
//
//   ┌─────────────────────────────────────────────────────────────────────────┐
//   │                         ALPHA ENGINE v1.2.0                             │
//   ├─────────────────────────────────────────────────────────────────────────┤
//   │                                                                         │
//   │   ┌───────────────┐       ┌───────────────┐                            │
//   │   │ XAUUSD Engine │       │ NAS100 Engine │                            │
//   │   │ (Winner-Bias) │       │ (Winner-Bias) │                            │
//   │   └───────┬───────┘       └───────┬───────┘                            │
//   │           │                       │                                     │
//   │           └───────────┬───────────┘                                     │
//   │                       │                                                 │
//   │              ┌────────▼────────┐                                        │
//   │              │  CTrader FIX    │                                        │
//   │              │  (shared)       │                                        │
//   │              └─────────────────┘                                        │
//   │                                                                         │
//   │   ┌───────────────┐  ┌───────────────┐  ┌───────────────┐              │
//   │   │  GUI Cockpit  │  │  ML Features  │  │    Logger     │              │
//   │   │  (Real-time)  │  │  (Training)   │  │  (CSV/Bin)    │              │
//   │   └───────────────┘  └───────────────┘  └───────────────┘              │
//   │                                                                         │
//   └─────────────────────────────────────────────────────────────────────────┘
//
// NEW IN v1.2.0:
// - Internal GUI cockpit (text-based, Dear ImGui ready)
// - ML feature extraction and logging
// - Enhanced trade logging with attribution
// - Tick data recording for backtesting
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "core/Types.hpp"
#include "session/SessionDetector.hpp"
#include "engine/InstrumentEngine.hpp"
#include "fix/CTraderFIXClient.hpp"
#include "fix/FIXConfig.hpp"
#include "gui/AlphaGUI.hpp"
#include "ml/MLFeatures.hpp"
#include "ml/MLLogger.hpp"
#include "logging/TradeLogger.hpp"
#include "DashboardServer.hpp"

#include <thread>
#include <atomic>
#include <array>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <mutex>

namespace Alpha {

// ═══════════════════════════════════════════════════════════════════════════════
// VERSION INFO (updated)
// ═══════════════════════════════════════════════════════════════════════════════
constexpr const char* ENGINE_VERSION = "1.2.0";
constexpr const char* ENGINE_CODENAME = "APEX-ML";

// ═══════════════════════════════════════════════════════════════════════════════
// ENGINE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
struct AlphaConfig {
    double initial_equity = 10000.0;
    bool shadow_mode = true;       // Paper trade (no real orders)
    bool verbose = false;
    
    // GUI settings
    bool gui_enabled = true;
    bool gui_text_mode = true;     // Text-based GUI (terminal)
    int gui_refresh_ms = 500;      // GUI refresh rate
    
    // ML settings
    bool ml_enabled = true;
    bool ml_log_ticks = true;
    bool ml_log_trades = true;
    int ml_sample_rate = 100;      // Log every N ticks
    
    // Logging settings
    bool log_trades = true;
    bool log_signals = true;
    bool log_ticks = false;        // Binary tick log (can be large)
    bool log_events = true;
    std::string log_dir = ".";
    
    // Dashboard settings
    bool dashboard_enabled = true;
    uint16_t dashboard_port = 8765;
    
    // Loaded from config.ini
    std::string config_path = "config.ini";
};

// ═══════════════════════════════════════════════════════════════════════════════
// ENGINE STATISTICS
// ═══════════════════════════════════════════════════════════════════════════════
struct AlphaStats {
    std::atomic<uint64_t> uptime_ms{0};
    std::atomic<uint64_t> ticks_total{0};
    std::atomic<uint64_t> trades_total{0};
    std::atomic<int64_t> pnl_bps{0};
    
    struct InstrumentStats {
        std::atomic<uint64_t> ticks{0};
        std::atomic<uint64_t> trades{0};
        std::atomic<int64_t> pnl_bps{0};
        std::atomic<int> wins{0};
        std::atomic<int> losses{0};
    };
    std::array<InstrumentStats, 2> instruments;
    
    // Daily stats
    std::atomic<int> daily_trades{0};
    std::atomic<int64_t> daily_pnl_bps{0};
    
    void print() const {
        std::cout << "\n"
                  << "╔════════════════════════════════════════════════════════════════╗\n"
                  << "║  ALPHA ENGINE STATISTICS                                       ║\n"
                  << "╠════════════════════════════════════════════════════════════════╣\n"
                  << "║  Uptime: " << std::setw(8) << (uptime_ms.load() / 1000) << "s"
                  << "  Ticks: " << std::setw(8) << ticks_total.load()
                  << "  Trades: " << std::setw(4) << trades_total.load() << "\n"
                  << "║  PnL: " << std::setw(8) << (pnl_bps.load() / 10.0) << " bps\n"
                  << "╠════════════════════════════════════════════════════════════════╣\n";
        
        for (size_t i = 0; i < 2; ++i) {
            auto inst = static_cast<Instrument>(i);
            double wr = (instruments[i].trades.load() > 0) 
                ? (double)instruments[i].wins.load() / instruments[i].trades.load() * 100.0
                : 0.0;
            std::cout << "║  " << std::setw(8) << instrument_str(inst) << ": "
                      << instruments[i].ticks.load() << " ticks, "
                      << instruments[i].trades.load() << " trades, "
                      << (instruments[i].pnl_bps.load() / 10.0) << " bps"
                      << " (WR: " << std::fixed << std::setprecision(1) << wr << "%)\n";
        }
        std::cout << "╚════════════════════════════════════════════════════════════════╝\n\n";
    }
    
    void reset_daily() {
        daily_trades.store(0);
        daily_pnl_bps.store(0);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ALPHA ENGINE
// ═══════════════════════════════════════════════════════════════════════════════
class AlphaEngine {
public:
    explicit AlphaEngine(const AlphaConfig& config = AlphaConfig{}) noexcept
        : config_(config)
        , running_(false)
        , gold_engine_(Instrument::XAUUSD, config.initial_equity)
        , nas_engine_(Instrument::NAS100, config.initial_equity)
    {
        // Setup order callbacks
        gold_engine_.set_order_callback([this](const OrderIntent& order) {
            return send_order(order);
        });
        
        nas_engine_.set_order_callback([this](const OrderIntent& order) {
            return send_order(order);
        });
    }
    
    ~AlphaEngine() {
        stop();
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // LIFECYCLE
    // ─────────────────────────────────────────────────────────────────────────
    bool start() noexcept {
        if (running_.load()) return false;
        
        print_banner();
        
        // ═══════════════════════════════════════════════════════════════════
        // INITIALIZE LOGGING
        // ═══════════════════════════════════════════════════════════════════
        if (config_.log_trades || config_.log_signals || config_.log_events) {
            Logging::TradeLogger::Config log_config;
            log_config.enabled = true;
            log_config.log_signals = config_.log_signals;
            log_config.log_trades = config_.log_trades;
            log_config.log_ticks = config_.log_ticks;
            log_config.log_events = config_.log_events;
            log_config.output_dir = config_.log_dir;
            log_config.console_output = config_.verbose;
            
            Logging::getLogger().config() = log_config;
            Logging::getLogger().start();
            
            Logging::getLogger().info("ENGINE", "Alpha Engine v" + std::string(ENGINE_VERSION) + " starting");
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // INITIALIZE ML LOGGING
        // ═══════════════════════════════════════════════════════════════════
        if (config_.ml_enabled) {
            ML::MLLogger::Config ml_config;
            ml_config.enabled = true;
            ml_config.log_ticks = config_.ml_log_ticks;
            ml_config.log_trades = config_.ml_log_trades;
            ml_config.tick_sample_rate = config_.ml_sample_rate;
            ml_config.output_dir = config_.log_dir;
            
            ML::getMLLogger().config() = ml_config;
            ML::getMLLogger().start();
            
            std::cout << "[ALPHA] ML logging enabled (sample rate: 1/" << config_.ml_sample_rate << ")\n";
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // INITIALIZE DASHBOARD SERVER
        // ═══════════════════════════════════════════════════════════════════
        if (config_.dashboard_enabled) {
            if (getDashboardServer().start()) {
                std::cout << "[ALPHA] Dashboard server started on port " << config_.dashboard_port << "\n";
            }
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // LOAD FIX CONFIG
        // ═══════════════════════════════════════════════════════════════════
        FIXConfig fix_config;
        if (!fix_config.isValid()) {
            std::cerr << "[ALPHA] ERROR: Invalid FIX configuration. Check config.ini\n";
            Logging::getLogger().error("ENGINE", "Invalid FIX configuration");
            return false;
        }
        fix_config.print();
        
        // Configure FIX client
        fix_client_.setConfig(fix_config);
        fix_client_.setExternalRunning(&running_);
        
        // Setup FIX callbacks
        fix_client_.setOnTick([this](const CTraderTick& tick) {
            on_fix_tick(tick);
        });
        
        fix_client_.setOnExec([this](const CTraderExecReport& report) {
            on_fix_exec(report);
        });
        
        fix_client_.setOnState([this](bool quote, bool trade) {
            on_fix_state(quote, trade);
        });
        
        running_.store(true);
        start_time_ = now_ms();
        
        // Connect to cTrader
        std::cout << "[ALPHA] Connecting to cTrader FIX...\n";
        Logging::getLogger().info("ENGINE", "Connecting to cTrader FIX");
        
        if (!fix_client_.connect()) {
            std::cerr << "[ALPHA] ERROR: Failed to connect to cTrader\n";
            Logging::getLogger().error("ENGINE", "Failed to connect to cTrader");
            running_.store(false);
            return false;
        }
        
        // Wait for security list
        std::cout << "[ALPHA] Requesting security list...\n";
        fix_client_.requestSecurityList();
        
        for (int i = 0; i < 100 && !fix_client_.isSecurityListReady() && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!fix_client_.isSecurityListReady()) {
            std::cerr << "[ALPHA] WARNING: Security list not received, proceeding anyway\n";
            Logging::getLogger().warn("ENGINE", "Security list not received");
        }
        
        // Subscribe to XAUUSD and NAS100
        std::cout << "[ALPHA] Subscribing to market data...\n";
        subscribe_instruments();
        
        // Start engines
        gold_engine_.start();
        nas_engine_.start();
        
        // Start main loop thread
        engine_thread_ = std::thread(&AlphaEngine::main_loop, this);
        
        // Start GUI thread if enabled
        if (config_.gui_enabled && config_.gui_text_mode) {
            gui_thread_ = std::thread(&AlphaEngine::gui_loop, this);
        }
        
        std::cout << "[ALPHA] Engine started successfully!\n";
        Logging::getLogger().info("ENGINE", "Engine started successfully");
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        std::cout << "[ALPHA] Stopping engine...\n";
        Logging::getLogger().info("ENGINE", "Engine stopping");
        
        running_.store(false);
        
        // Stop engines
        gold_engine_.stop();
        nas_engine_.stop();
        
        // Disconnect FIX
        fix_client_.disconnect();
        
        // Wait for threads
        if (engine_thread_.joinable()) {
            engine_thread_.join();
        }
        if (gui_thread_.joinable()) {
            gui_thread_.join();
        }
        
        // Stop logging systems
        ML::getMLLogger().stop();
        Logging::getLogger().stop();
        getDashboardServer().stop();
        
        // Print final stats
        stats_.uptime_ms.store(now_ms() - start_time_);
        stats_.print();
        
        std::cout << "[ALPHA] Engine stopped.\n";
    }
    
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] const AlphaStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const AlphaConfig& config() const noexcept { return config_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // RUNTIME CONTROLS
    // ─────────────────────────────────────────────────────────────────────────
    void toggle_shadow_mode() noexcept {
        config_.shadow_mode = !config_.shadow_mode;
        std::cout << "[ALPHA] Mode: " << (config_.shadow_mode ? "SHADOW" : "LIVE") << "\n";
        Logging::getLogger().info("ENGINE", 
            std::string("Mode changed to ") + (config_.shadow_mode ? "SHADOW" : "LIVE"));
    }
    
    void kill(const std::string& reason = "MANUAL") noexcept {
        getKillSwitch().kill(reason.c_str());
        std::cout << "[ALPHA] KILL SWITCH ACTIVATED: " << reason << "\n";
        Logging::getLogger().warn("ENGINE", "Kill switch activated: " + reason);
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // FIX CALLBACKS
    // ─────────────────────────────────────────────────────────────────────────
    void on_fix_tick(const CTraderTick& fix_tick) noexcept {
        // Parse instrument
        Instrument inst = parse_instrument(fix_tick.symbol);
        if (inst == Instrument::INVALID) return;
        
        // Create Alpha tick
        Tick tick = Tick::make(inst, fix_tick.bid, fix_tick.ask, now_ns(), stats_.ticks_total.load());
        
        // Update stats
        stats_.ticks_total.fetch_add(1);
        stats_.instruments[static_cast<size_t>(inst)].ticks.fetch_add(1);
        
        // ═══════════════════════════════════════════════════════════════════
        // ML FEATURE EXTRACTION
        // ═══════════════════════════════════════════════════════════════════
        if (config_.ml_enabled) {
            ml_extractors_.update(tick);
            
            if (ml_extractors_.ready(inst)) {
                auto fv = ml_extractors_.extract(inst);
                
                // Add position features if applicable
                if (inst == Instrument::XAUUSD && gold_engine_.has_position()) {
                    // Would need access to position state - placeholder
                }
                
                ML::getMLLogger().log_features(fv);
                
                // Update GUI with features
                GUI::getGUI().update_ml_features(inst, fv);
            }
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // TICK LOGGING (for replay)
        // ═══════════════════════════════════════════════════════════════════
        if (config_.log_ticks) {
            Logging::getLogger().log_tick(tick);
        }
        
        // Route to correct engine
        if (inst == Instrument::XAUUSD) {
            gold_engine_.on_tick(tick);
        } else if (inst == Instrument::NAS100) {
            nas_engine_.on_tick(tick);
        }
        
        // Update latest prices for GUI
        update_gui_prices(inst, tick);
    }
    
    void on_fix_exec(const CTraderExecReport& report) noexcept {
        Instrument inst = parse_instrument(report.symbol);
        if (inst == Instrument::INVALID) return;
        
        if (report.isFill()) {
            Side side = (report.side == FIXSide::Buy) ? Side::LONG : Side::SHORT;
            bool is_close = false;  // TODO: Track from OrderIntent
            
            if (inst == Instrument::XAUUSD) {
                gold_engine_.on_fill(side, report.lastPx, report.lastQty, is_close);
            } else {
                nas_engine_.on_fill(side, report.lastPx, report.lastQty, is_close);
            }
            
            stats_.trades_total.fetch_add(1);
            stats_.daily_trades.fetch_add(1);
            stats_.instruments[static_cast<size_t>(inst)].trades.fetch_add(1);
            
            Logging::getLogger().info("FILL", 
                std::string(instrument_str(inst)) + " " + side_str(side) + 
                " @ " + std::to_string(report.lastPx), inst);
        }
        
        if (report.isReject()) {
            std::cerr << "[ALPHA] ORDER REJECTED: " << report.text << "\n";
            Logging::getLogger().error("ORDER", "Rejected: " + std::string(report.text), inst);
        }
    }
    
    void on_fix_state(bool quote_connected, bool trade_connected) noexcept {
        fix_quote_connected_ = quote_connected;
        fix_trade_connected_ = trade_connected;
        
        std::cout << "[ALPHA] FIX State: QUOTE=" << (quote_connected ? "UP" : "DOWN")
                  << " TRADE=" << (trade_connected ? "UP" : "DOWN") << "\n";
        
        Logging::getLogger().info("FIX", 
            std::string("State: QUOTE=") + (quote_connected ? "UP" : "DOWN") + 
            " TRADE=" + (trade_connected ? "UP" : "DOWN"));
        
        if (!quote_connected || !trade_connected) {
            // Handle reconnection
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // ORDER SENDING
    // ─────────────────────────────────────────────────────────────────────────
    bool send_order(const OrderIntent& order) noexcept {
        if (!order.valid()) return false;
        if (!fix_client_.isConnected()) {
            std::cerr << "[ALPHA] Cannot send order: FIX not connected\n";
            Logging::getLogger().error("ORDER", "FIX not connected", order.instrument);
            return false;
        }
        
        std::string symbol = instrument_str(order.instrument);
        char side = (order.side == Side::LONG) ? FIXSide::Buy : FIXSide::Sell;
        char posEffect = order.is_close ? FIXPositionEffect::Close : FIXPositionEffect::Open;
        
        std::cout << "[ALPHA] Sending order: " << symbol << " "
                  << (order.side == Side::LONG ? "BUY" : "SELL") << " "
                  << std::fixed << std::setprecision(2) << order.size
                  << (order.is_close ? " (CLOSE)" : " (OPEN)") << "\n";
        
        Logging::getLogger().info("ORDER", 
            symbol + " " + (order.side == Side::LONG ? "BUY" : "SELL") + 
            " " + std::to_string(order.size) + (order.is_close ? " CLOSE" : " OPEN"),
            order.instrument);
        
        if (config_.shadow_mode) {
            std::cout << "[ALPHA] SHADOW MODE - Order not sent\n";
            // Simulate fill for shadow mode
            InstrumentEngine* engine = (order.instrument == Instrument::XAUUSD) 
                                        ? &gold_engine_ : &nas_engine_;
            engine->on_fill(order.side, order.entry_price, order.size, order.is_close);
            return true;
        }
        
        return fix_client_.sendMarketOrder(symbol, side, order.size, posEffect, order.entry_price);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // SUBSCRIPTION
    // ─────────────────────────────────────────────────────────────────────────
    void subscribe_instruments() noexcept {
        // Try various symbol names
        std::vector<std::string> gold_names = {"XAUUSD", "XAUUSDm", "GOLD"};
        std::vector<std::string> nas_names = {"NAS100", "NAS100m", "US100", "USTEC"};
        
        bool gold_subscribed = false, nas_subscribed = false;
        
        for (const auto& name : gold_names) {
            if (fix_client_.subscribeMarketData(name)) {
                std::cout << "[ALPHA] Subscribed to " << name << "\n";
                Logging::getLogger().info("FIX", "Subscribed to " + name, Instrument::XAUUSD);
                gold_subscribed = true;
                break;
            }
        }
        
        for (const auto& name : nas_names) {
            if (fix_client_.subscribeMarketData(name)) {
                std::cout << "[ALPHA] Subscribed to " << name << "\n";
                Logging::getLogger().info("FIX", "Subscribed to " + name, Instrument::NAS100);
                nas_subscribed = true;
                break;
            }
        }
        
        if (!gold_subscribed) {
            std::cerr << "[ALPHA] WARNING: Could not subscribe to XAUUSD\n";
            Logging::getLogger().warn("FIX", "Could not subscribe to XAUUSD");
        }
        if (!nas_subscribed) {
            std::cerr << "[ALPHA] WARNING: Could not subscribe to NAS100\n";
            Logging::getLogger().warn("FIX", "Could not subscribe to NAS100");
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // GUI UPDATES
    // ─────────────────────────────────────────────────────────────────────────
    void update_gui_prices(Instrument inst, const Tick& tick) noexcept {
        GUI::EngineDisplayData data;
        data.instrument = inst;
        data.bid = tick.bid;
        data.ask = tick.ask;
        data.spread = tick.spread;
        data.spread_bps = tick.spread_bps;
        
        InstrumentEngine* engine = (inst == Instrument::XAUUSD) ? &gold_engine_ : &nas_engine_;
        data.state = engine->state();
        data.tick_count = engine->tick_count();
        data.trade_count = stats_.instruments[static_cast<size_t>(inst)].trades.load();
        data.pnl_bps = stats_.instruments[static_cast<size_t>(inst)].pnl_bps.load() / 10.0;
        data.regime = regime_str(engine->regime());
        data.atr_ratio = engine->atr_ratio();
        data.has_position = engine->has_position();
        
        if (engine->has_position()) {
            data.position_r = engine->current_r();
        }
        
        auto session = current_session(inst);
        data.session = session.type;
        data.session_multiplier = session.size_multiplier;
        data.is_peak = session.is_peak;
        
        GUI::getGUI().update_engine(data);
    }
    
    void update_gui_system() noexcept {
        GUI::SystemDisplayData data;
        data.fix_quote_connected = fix_quote_connected_;
        data.fix_trade_connected = fix_trade_connected_;
        data.uptime_ms = stats_.uptime_ms.load();
        data.start_time = start_time_;
        data.shadow_mode = config_.shadow_mode;
        data.kill_switch_active = getKillSwitch().killed();
        data.kill_reason = getKillSwitch().reason();
        data.total_ticks = stats_.ticks_total.load();
        data.total_trades = stats_.trades_total.load();
        data.total_pnl_bps = stats_.pnl_bps.load() / 10.0;
        data.daily_trades = stats_.daily_trades.load();
        data.daily_pnl_bps = stats_.daily_pnl_bps.load() / 10.0;
        data.dashboard_clients = getDashboardServer().client_count();
        data.ml_enabled = config_.ml_enabled;
        data.ml_features_logged = ML::getMLLogger().features_logged();
        data.ml_trades_logged = ML::getMLLogger().trades_logged();
        
        GUI::getGUI().update_system(data);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // MAIN LOOP
    // ─────────────────────────────────────────────────────────────────────────
    void main_loop() noexcept {
        std::cout << "[ALPHA] Main loop started\n";
        Logging::getLogger().info("ENGINE", "Main loop started");
        
        uint64_t last_status = now_ms();
        uint64_t last_gui_update = now_ms();
        uint64_t last_dashboard = now_ms();
        
        while (running_.load() && getKillSwitch().alive()) {
            stats_.uptime_ms.store(now_ms() - start_time_);
            
            // ═══════════════════════════════════════════════════════════════
            // GUI SYSTEM UPDATE (every 100ms)
            // ═══════════════════════════════════════════════════════════════
            if (now_ms() - last_gui_update >= 100) {
                update_gui_system();
                last_gui_update = now_ms();
                
                // Check for GUI controls
                if (GUI::getGUI().kill_requested()) {
                    kill("GUI_REQUEST");
                }
                if (GUI::getGUI().shadow_toggle_requested()) {
                    toggle_shadow_mode();
                }
            }
            
            // ═══════════════════════════════════════════════════════════════
            // DASHBOARD BROADCAST (every 500ms)
            // ═══════════════════════════════════════════════════════════════
            if (config_.dashboard_enabled && now_ms() - last_dashboard >= 500) {
                broadcast_dashboard();
                last_dashboard = now_ms();
            }
            
            // ═══════════════════════════════════════════════════════════════
            // PERIODIC STATUS (every 60 seconds)
            // ═══════════════════════════════════════════════════════════════
            if (now_ms() - last_status >= 60000) {
                print_status();
                last_status = now_ms();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        std::cout << "[ALPHA] Main loop exited\n";
        Logging::getLogger().info("ENGINE", "Main loop exited");
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // GUI LOOP (text-based for now)
    // ─────────────────────────────────────────────────────────────────────────
    void gui_loop() noexcept {
        std::cout << "[ALPHA] GUI loop started\n";
        
        while (running_.load() && getKillSwitch().alive()) {
            // Render text-based GUI
            std::string output = GUI::getGUI().render_text();
            std::cout << output << std::flush;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.gui_refresh_ms));
        }
        
        std::cout << "[ALPHA] GUI loop exited\n";
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // DASHBOARD BROADCAST
    // ─────────────────────────────────────────────────────────────────────────
    void broadcast_dashboard() noexcept {
        DashboardStatus status;
        status.uptime_s = stats_.uptime_ms.load() / 1000;
        status.total_ticks = stats_.ticks_total.load();
        status.total_trades = stats_.trades_total.load();
        status.total_pnl = stats_.pnl_bps.load() / 10.0;
        status.daily_trades = stats_.daily_trades.load();
        status.daily_pnl = stats_.daily_pnl_bps.load() / 10.0;
        
        // XAUUSD status
        status.xauusd.instrument = Instrument::XAUUSD;
        status.xauusd.state = gold_engine_.state();
        status.xauusd.ticks = gold_engine_.tick_count();
        status.xauusd.trades = stats_.instruments[0].trades.load();
        status.xauusd.pnl_bps = stats_.instruments[0].pnl_bps.load() / 10.0;
        status.xauusd.regime = regime_str(gold_engine_.regime());
        status.xauusd.spread_bps = gold_engine_.spread_bps();
        status.xauusd.has_position = gold_engine_.has_position();
        if (gold_engine_.has_position()) {
            status.xauusd.pos_pnl_bps = gold_engine_.current_r();
        }
        
        // NAS100 status
        status.nas100.instrument = Instrument::NAS100;
        status.nas100.state = nas_engine_.state();
        status.nas100.ticks = nas_engine_.tick_count();
        status.nas100.trades = stats_.instruments[1].trades.load();
        status.nas100.pnl_bps = stats_.instruments[1].pnl_bps.load() / 10.0;
        status.nas100.regime = regime_str(nas_engine_.regime());
        status.nas100.spread_bps = nas_engine_.spread_bps();
        status.nas100.has_position = nas_engine_.has_position();
        if (nas_engine_.has_position()) {
            status.nas100.pos_pnl_bps = nas_engine_.current_r();
        }
        
        getDashboardServer().broadcast_status(status);
    }
    
    void print_status() noexcept {
        if (config_.gui_enabled) return;  // GUI handles this
        
        std::cout << "\n[ALPHA] Status @ " << (stats_.uptime_ms.load() / 1000) << "s:\n";
        
        // XAUUSD status
        auto gold_session = current_session(Instrument::XAUUSD);
        std::cout << "  XAUUSD: state=" << engine_state_str(gold_engine_.state())
                  << " session=" << session_str(gold_session.session)
                  << " regime=" << regime_str(gold_engine_.regime())
                  << " ticks=" << gold_engine_.tick_count()
                  << " atr=" << std::fixed << std::setprecision(2) << gold_engine_.atr_ratio();
        if (gold_engine_.has_position()) {
            std::cout << " R=" << std::setprecision(2) << gold_engine_.current_r();
        }
        std::cout << "\n";
        
        // NAS100 status
        auto nas_session = current_session(Instrument::NAS100);
        std::cout << "  NAS100: state=" << engine_state_str(nas_engine_.state())
                  << " session=" << session_str(nas_session.session)
                  << " regime=" << regime_str(nas_engine_.regime())
                  << " ticks=" << nas_engine_.tick_count()
                  << " atr=" << std::fixed << std::setprecision(2) << nas_engine_.atr_ratio();
        if (nas_engine_.has_position()) {
            std::cout << " R=" << std::setprecision(2) << nas_engine_.current_r();
        }
        std::cout << "\n";
    }
    
    void print_banner() const noexcept {
        std::cout << "\n"
            << "╔════════════════════════════════════════════════════════════════╗\n"
            << "║                                                                ║\n"
            << "║     █████╗ ██╗     ██████╗ ██╗  ██╗ █████╗                     ║\n"
            << "║    ██╔══██╗██║     ██╔══██╗██║  ██║██╔══██╗                    ║\n"
            << "║    ███████║██║     ██████╔╝███████║███████║                    ║\n"
            << "║    ██╔══██║██║     ██╔═══╝ ██╔══██║██╔══██║                    ║\n"
            << "║    ██║  ██║███████╗██║     ██║  ██║██║  ██║                    ║\n"
            << "║    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝                    ║\n"
            << "║                                                                ║\n"
            << "║    VERSION: " << ENGINE_VERSION << " (" << ENGINE_CODENAME << ")                               ║\n"
            << "║    MODE: " << (config_.shadow_mode ? "SHADOW (Paper)" : "LIVE          ")
            << "                          ║\n"
            << "║    INSTRUMENTS: XAUUSD, NAS100                                 ║\n"
            << "║    FEATURES: GUI + ML + Logging                                ║\n"
            << "║                                                                ║\n"
            << "╚════════════════════════════════════════════════════════════════╝\n\n";
    }
    
    AlphaConfig config_;
    AlphaStats stats_;
    
    std::atomic<bool> running_;
    std::thread engine_thread_;
    std::thread gui_thread_;
    uint64_t start_time_ = 0;
    
    // Dual engines
    InstrumentEngine gold_engine_;
    InstrumentEngine nas_engine_;
    
    // Shared FIX client
    CTraderFIXClient fix_client_;
    bool fix_quote_connected_ = false;
    bool fix_trade_connected_ = false;
    
    // ML feature extractors
    ML::DualFeatureExtractor ml_extractors_;
};

}  // namespace Alpha
