// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Main Engine
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.0.0
// ARCHITECTURE: Dual-engine (XAUUSD + NAS100) with shared FIX connection
//
//   ┌─────────────────────────────────────────────────────────────────┐
//   │                      ALPHA ENGINE                               │
//   ├─────────────────────────────────────────────────────────────────┤
//   │                                                                 │
//   │   ┌─────────────────┐         ┌─────────────────┐              │
//   │   │  XAUUSD Engine  │         │  NAS100 Engine  │              │
//   │   │  (CPU affinity) │         │  (CPU affinity) │              │
//   │   └────────┬────────┘         └────────┬────────┘              │
//   │            │                           │                        │
//   │            └───────────┬───────────────┘                        │
//   │                        │                                        │
//   │              ┌─────────▼─────────┐                              │
//   │              │  CTrader FIX      │                              │
//   │              │  (shared client)  │                              │
//   │              └───────────────────┘                              │
//   │                                                                 │
//   └─────────────────────────────────────────────────────────────────┘
//
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "core/Types.hpp"
#include "session/SessionDetector.hpp"
#include "engine/InstrumentEngine.hpp"
#include "fix/CTraderFIXClient.hpp"
#include "fix/FIXConfig.hpp"

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
// ENGINE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
struct AlphaConfig {
    double initial_equity = 10000.0;
    bool shadow_mode = true;       // Paper trade (no real orders)
    bool log_trades = true;
    bool verbose = false;
    
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
    };
    std::array<InstrumentStats, 2> instruments;
    
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
            std::cout << "║  " << std::setw(8) << instrument_str(inst) << ": "
                      << instruments[i].ticks.load() << " ticks, "
                      << instruments[i].trades.load() << " trades, "
                      << (instruments[i].pnl_bps.load() / 10.0) << " bps\n";
        }
        std::cout << "╚════════════════════════════════════════════════════════════════╝\n\n";
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
        
        if (config.log_trades) {
            init_trade_log();
        }
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
        
        // Load FIX config
        FIXConfig fix_config;
        if (!fix_config.isValid()) {
            std::cerr << "[ALPHA] ERROR: Invalid FIX configuration. Check config.ini\n";
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
        if (!fix_client_.connect()) {
            std::cerr << "[ALPHA] ERROR: Failed to connect to cTrader\n";
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
        }
        
        // Subscribe to XAUUSD and NAS100
        std::cout << "[ALPHA] Subscribing to market data...\n";
        subscribe_instruments();
        
        // Start engines
        gold_engine_.start();
        nas_engine_.start();
        
        // Start main loop thread
        engine_thread_ = std::thread(&AlphaEngine::main_loop, this);
        
        std::cout << "[ALPHA] Engine started successfully!\n";
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        std::cout << "[ALPHA] Stopping engine...\n";
        running_.store(false);
        
        // Stop engines
        gold_engine_.stop();
        nas_engine_.stop();
        
        // Disconnect FIX
        fix_client_.disconnect();
        
        // Wait for main thread
        if (engine_thread_.joinable()) {
            engine_thread_.join();
        }
        
        // Close trade log
        if (trade_log_.is_open()) {
            trade_log_.close();
        }
        
        // Print final stats
        stats_.uptime_ms.store(now_ms() - start_time_);
        stats_.print();
        
        std::cout << "[ALPHA] Engine stopped.\n";
    }
    
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] const AlphaStats& stats() const noexcept { return stats_; }

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
        
        // Route to correct engine
        if (inst == Instrument::XAUUSD) {
            gold_engine_.on_tick(tick);
        } else if (inst == Instrument::NAS100) {
            nas_engine_.on_tick(tick);
        }
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
            stats_.instruments[static_cast<size_t>(inst)].trades.fetch_add(1);
        }
        
        if (report.isReject()) {
            std::cerr << "[ALPHA] ORDER REJECTED: " << report.text << "\n";
        }
    }
    
    void on_fix_state(bool quote_connected, bool trade_connected) noexcept {
        std::cout << "[ALPHA] FIX State: QUOTE=" << (quote_connected ? "UP" : "DOWN")
                  << " TRADE=" << (trade_connected ? "UP" : "DOWN") << "\n";
        
        if (!quote_connected || !trade_connected) {
            // TODO: Handle reconnection
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // ORDER SENDING
    // ─────────────────────────────────────────────────────────────────────────
    bool send_order(const OrderIntent& order) noexcept {
        if (!order.valid()) return false;
        if (!fix_client_.isConnected()) {
            std::cerr << "[ALPHA] Cannot send order: FIX not connected\n";
            return false;
        }
        
        std::string symbol = instrument_str(order.instrument);
        char side = (order.side == Side::LONG) ? FIXSide::Buy : FIXSide::Sell;
        char posEffect = order.is_close ? FIXPositionEffect::Close : FIXPositionEffect::Open;
        
        std::cout << "[ALPHA] Sending order: " << symbol << " "
                  << (order.side == Side::LONG ? "BUY" : "SELL") << " "
                  << std::fixed << std::setprecision(2) << order.size
                  << (order.is_close ? " (CLOSE)" : " (OPEN)") << "\n";
        
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
                gold_subscribed = true;
                break;
            }
        }
        
        for (const auto& name : nas_names) {
            if (fix_client_.subscribeMarketData(name)) {
                std::cout << "[ALPHA] Subscribed to " << name << "\n";
                nas_subscribed = true;
                break;
            }
        }
        
        if (!gold_subscribed) std::cerr << "[ALPHA] WARNING: Could not subscribe to XAUUSD\n";
        if (!nas_subscribed) std::cerr << "[ALPHA] WARNING: Could not subscribe to NAS100\n";
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // MAIN LOOP
    // ─────────────────────────────────────────────────────────────────────────
    void main_loop() noexcept {
        std::cout << "[ALPHA] Main loop started\n";
        
        uint64_t last_status = now_ms();
        
        while (running_.load() && getKillSwitch().alive()) {
            stats_.uptime_ms.store(now_ms() - start_time_);
            
            // Periodic status (every 60 seconds)
            if (now_ms() - last_status >= 60000) {
                print_status();
                last_status = now_ms();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "[ALPHA] Main loop exited\n";
    }
    
    void print_status() noexcept {
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
    
    // ─────────────────────────────────────────────────────────────────────────
    // LOGGING
    // ─────────────────────────────────────────────────────────────────────────
    void init_trade_log() noexcept {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        char filename[64];
        std::strftime(filename, sizeof(filename), "alpha_trades_%Y%m%d.csv", tm);
        
        trade_log_.open(filename, std::ios::app);
        if (trade_log_.is_open() && trade_log_.tellp() == 0) {
            trade_log_ << "timestamp,instrument,side,size,entry,exit,pnl_bps,hold_ms,reason\n";
        }
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
            << "║    VERSION: " << VERSION << " (" << CODENAME << ")                               ║\n"
            << "║    MODE: " << (config_.shadow_mode ? "SHADOW (Paper)" : "LIVE          ")
            << "                          ║\n"
            << "║    INSTRUMENTS: XAUUSD, NAS100                                 ║\n"
            << "║    ARCHITECTURE: Dual-Engine                                   ║\n"
            << "║                                                                ║\n"
            << "╚════════════════════════════════════════════════════════════════╝\n\n";
    }
    
    AlphaConfig config_;
    AlphaStats stats_;
    
    std::atomic<bool> running_;
    std::thread engine_thread_;
    uint64_t start_time_ = 0;
    
    // Dual engines
    InstrumentEngine gold_engine_;
    InstrumentEngine nas_engine_;
    
    // Shared FIX client
    CTraderFIXClient fix_client_;
    
    std::ofstream trade_log_;
};

}  // namespace Alpha
