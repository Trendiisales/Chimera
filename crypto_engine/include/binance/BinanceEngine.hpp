// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceEngine.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Main Binance trading engine - owns all symbol threads and connections
// OWNER: Jo
// LAST VERIFIED: 2024-12-24
//
// v6.97 FIXES:
//   - Implemented REAL PnL tracking (was hardcoded pnl=0.0)
//   - Added position tracker per symbol for entry price tracking
//   - Added win/loss counters
//   - Fixed latency tracking to update per-message
//
// DESIGN:
// - Owns market data WebSocket connection (shared by all symbols)
// - Owns OrderSender thread (shared by all symbols)
// - Owns one SymbolThread per symbol (BTCUSDT, ETHUSDT, SOLUSDT)
// - Dispatches incoming data to appropriate symbol threads
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <thread>
#include <chrono>  // v4.9.13: For time sync retry backoff
#include <atomic>
#include <unordered_map>
#include <functional>  // v4.2.2: For shadow trade callback

#include "BinanceConfig.hpp"
#include "BinanceWebSocket.hpp"
#include "BinanceParser.hpp"
#include "BinanceRestClient.hpp"  // v4.9.13: For server time sync
#include "SymbolThread.hpp"
#include "BinanceOrderSender.hpp"

#include "../core/GlobalKill.hpp"
#include "../risk/DailyLossGuard.hpp"
#include "latency/HotPathLatencyTracker.hpp"  // v4.9.10: Hot-path latency
#include "bootstrap/LatencyBootstrapper.hpp"  // v4.9.10: Bootstrap probes
#include "runtime/SystemMode.hpp"             // v4.9.10: System mode

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// Engine State
// ─────────────────────────────────────────────────────────────────────────────
enum class EngineState : uint8_t {
    STOPPED     = 0,
    STARTING    = 1,
    CONNECTING  = 2,
    SYNCING     = 3,  // Getting initial snapshots
    RUNNING     = 4,
    STOPPING    = 5,
    ERROR       = 6
};

// ─────────────────────────────────────────────────────────────────────────────
// v6.97: Position Tracker for PnL calculation
// ─────────────────────────────────────────────────────────────────────────────
struct PositionTracker {
    double quantity = 0.0;      // Current position (positive=long, negative=short)
    double avg_entry_price = 0.0;  // Average entry price
    double realized_pnl = 0.0;  // Total realized PnL for this symbol
    uint32_t wins = 0;
    uint32_t losses = 0;
    uint32_t trades = 0;
    
    // Process a fill and return the realized PnL (0 if still building position)
    double on_fill(Side side, double fill_qty, double fill_price) {
        double pnl = 0.0;
        double signed_qty = (side == Side::Buy) ? fill_qty : -fill_qty;
        
        // Check if this is reducing or flipping position
        bool reducing = (quantity > 0 && side == Side::Sell) || 
                       (quantity < 0 && side == Side::Buy);
        
        if (reducing && quantity != 0.0) {
            // Calculate PnL on the closed portion
            double close_qty = std::min(std::abs(signed_qty), std::abs(quantity));
            if (quantity > 0) {
                // Closing long: PnL = (exit - entry) * qty
                pnl = (fill_price - avg_entry_price) * close_qty;
            } else {
                // Closing short: PnL = (entry - exit) * qty
                pnl = (avg_entry_price - fill_price) * close_qty;
            }
            
            realized_pnl += pnl;
            trades++;
            if (pnl > 0) wins++;
            else if (pnl < 0) losses++;
            
            // Update position
            double old_qty = quantity;
            quantity += signed_qty;
            
            // If position flipped, set new entry price
            if ((old_qty > 0 && quantity < 0) || (old_qty < 0 && quantity > 0)) {
                avg_entry_price = fill_price;
            } else if (std::abs(quantity) < 0.0000001) {
                // Position closed
                quantity = 0.0;
                avg_entry_price = 0.0;
            }
        } else {
            // Adding to position or opening new
            if (quantity == 0.0) {
                // New position
                avg_entry_price = fill_price;
                quantity = signed_qty;
            } else {
                // Adding to existing position - update average entry
                double total_cost = (avg_entry_price * std::abs(quantity)) + 
                                   (fill_price * fill_qty);
                quantity += signed_qty;
                avg_entry_price = total_cost / std::abs(quantity);
            }
        }
        
        return pnl;
    }
    
    void reset() {
        quantity = 0.0;
        avg_entry_price = 0.0;
        realized_pnl = 0.0;
        wins = losses = trades = 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Tick Callback (for GUI updates - v4.5.0)
// ─────────────────────────────────────────────────────────────────────────────
using TickCallback = std::function<void(const char* symbol, double bid, double ask, 
                                        double bid_qty, double ask_qty, double latency_ms)>;

// ─────────────────────────────────────────────────────────────────────────────
// v4.9.7: External Fill Callback (for MicroScalp engine integration)
// ─────────────────────────────────────────────────────────────────────────────
// Called when fills are received - allows MicroScalp engines to transition from
// pending_fill to PROBE state. Parameters: symbol, is_buy, qty, price
using ExternalFillCallback = std::function<void(const char* symbol, bool is_buy, double qty, double price)>;

// ─────────────────────────────────────────────────────────────────────────────
// Binance Engine
// ─────────────────────────────────────────────────────────────────────────────
class BinanceEngine {
public:
    BinanceEngine(GlobalKill& global_kill, DailyLossGuard& daily_loss) noexcept
        : global_kill_(global_kill)
        , daily_loss_(daily_loss)
        , config_(get_config())
        , rest_client_(config_.api_key, config_.secret_key)  // v4.9.13: For time sync
        , state_(EngineState::STOPPED)
        , running_(false)
        , order_sender_(order_queue_, global_kill, config_)
    {
        // Create symbol threads (v7.11: pass testnet flag)
        for (size_t i = 0; i < NUM_SYMBOLS; ++i) {
            symbol_threads_[i] = std::make_unique<SymbolThread>(
                SYMBOLS[i], global_kill, daily_loss, order_queue_, config_.is_testnet
            );
            // v6.97: Initialize position trackers
            position_trackers_[SYMBOLS[i].id] = PositionTracker{};
        }
        
        // Set up order sender callbacks
        order_sender_.set_on_fill([this](uint16_t sym_id, Side side, double qty, double price, FillType /*fill_type*/) {
            on_fill(sym_id, side, qty, price);
        });
        
        order_sender_.set_on_reject([this](uint16_t sym_id, const char* reason) {
            on_reject(sym_id, reason);
        });
        
        if (config_.is_testnet) {
            std::cout << "[BinanceEngine] *** TESTNET MODE - relaxed trading thresholds ***\n";
        }
    }
    
    ~BinanceEngine() {
        stop();
    }
    
    // Non-copyable
    BinanceEngine(const BinanceEngine&) = delete;
    BinanceEngine& operator=(const BinanceEngine&) = delete;
    
    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool start() noexcept {
        std::cout << "[BinanceEngine] start() entered\n";
        
        if (running_.load()) {
            std::cout << "[BinanceEngine] Already running\n";
            return true;
        }
        
        state_ = EngineState::STARTING;
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.13 CRITICAL: Sync with Binance server time BEFORE any signed requests
        // This fixes "Signature not valid" errors caused by clock drift
        // Retry up to 3 times with 1s backoff
        // ═══════════════════════════════════════════════════════════════════
        std::cout << "[BinanceEngine] Synchronizing with Binance server time...\n";
        uint64_t server_time_ms = 0;
        bool time_sync_ok = false;
        for (int retry = 0; retry < 3 && !time_sync_ok; ++retry) {
            if (retry > 0) {
                std::cout << "[BinanceEngine] Time sync retry " << retry << "/3...\n";
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (rest_client_.get_server_time(server_time_ms)) {
                Binance::BinanceTimeSync::set_offset(static_cast<int64_t>(server_time_ms));
                Binance::BinanceTimeSync::mark_initialized();
                std::cout << "[BinanceEngine] ✓ Time sync complete (offset: " 
                          << Binance::BinanceTimeSync::offset_ms() << " ms)\n";
                time_sync_ok = true;
            }
        }
        
        if (!time_sync_ok) {
            std::cerr << "[BinanceEngine] ⚠️  CRITICAL: Could not sync Binance server time after 3 retries!\n";
            std::cerr << "[BinanceEngine] Trading DISABLED until time sync succeeds.\n";
            std::cerr << "[BinanceEngine] Market data will still flow.\n";
        }
        
        // Build combined stream path
        char stream_path[512];
        build_combined_stream_path(stream_path, sizeof(stream_path));
        std::cout << "[BinanceEngine] Stream path: " << stream_path << "\n";
        std::cout << "[BinanceEngine] Host: " << config_.ws_stream_host << " Port: " << config_.ws_stream_port << "\n";
        
        // Connect market data WebSocket
        state_ = EngineState::CONNECTING;
        std::cout << "[BinanceEngine] Connecting to WebSocket...\n";
        if (!market_ws_.connect(config_.ws_stream_host, config_.ws_stream_port, stream_path)) {
            std::cerr << "[BinanceEngine] FAILED: WebSocket connect failed\n";
            state_ = EngineState::ERROR;
            return false;
        }
        std::cout << "[BinanceEngine] WebSocket connected\n";
        
        // Start order sender (non-fatal - allow MD-only mode)
        std::cout << "[BinanceEngine] Starting order sender...\n";
        if (!order_sender_.start()) {
            std::cerr << "[BinanceEngine] WARNING: Order sender disabled (MD-only mode)\n";
            // Don't abort - continue with market data only
        } else {
            std::cout << "[BinanceEngine] Order sender started - TRADING ENABLED\n";
        }
        
        // Get initial snapshots
        state_ = EngineState::SYNCING;
        std::cout << "[BinanceEngine] Fetching initial snapshots...\n";
        if (!fetch_initial_snapshots()) {
            std::cerr << "[BinanceEngine] FAILED: Initial snapshot fetch failed\n";
            market_ws_.disconnect();
            order_sender_.stop();
            state_ = EngineState::ERROR;
            return false;
        }
        std::cout << "[BinanceEngine] Initial snapshots OK\n";
        
        // Start symbol threads
        std::cout << "[BinanceEngine] Starting " << symbol_threads_.size() << " symbol threads\n";
        for (auto& thread : symbol_threads_) {
            thread->start();
        }
        
        // Start dispatcher thread
        running_.store(true);
        dispatcher_thread_ = std::thread(&BinanceEngine::dispatcher_loop, this);
        
        state_ = EngineState::RUNNING;
        std::cout << "[BinanceEngine] RUNNING\n";
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        state_ = EngineState::STOPPING;
        running_.store(false);
        
        // v4.9.9: EMERGENCY SHUTDOWN - Cancel orders and flatten positions FIRST
        printf("[BinanceEngine] EMERGENCY SHUTDOWN - Cancelling orders and flattening positions...\n");
        
        // Cancel all pending orders
        order_sender_.cancelAllOpenOrders();
        
        // Flatten any open positions
        for (const auto& [symbol_id, tracker] : position_trackers_) {
            if (std::abs(tracker.quantity) > 0.0000001) {
                const char* symbol = nullptr;
                for (size_t i = 0; i < SYMBOLS.size(); ++i) {
                    if (static_cast<uint16_t>(SYMBOLS[i].id) == symbol_id) {
                        symbol = SYMBOLS[i].symbol;
                        break;
                    }
                }
                if (symbol) {
                    bool is_long = tracker.quantity > 0;
                    double qty = std::abs(tracker.quantity);
                    printf("[BinanceEngine] FLATTEN: %s pos=%.6f %s\n", 
                           symbol, qty, is_long ? "(LONG→SELL)" : "(SHORT→BUY)");
                    order_sender_.sendEmergencyFlatten(symbol, qty, is_long);
                }
            }
        }
        
        // Give a moment for emergency orders to send
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Disconnect WebSockets to unblock any reads
        market_ws_.disconnect();
        
        // Stop dispatcher with timeout (3 seconds max)
        if (dispatcher_thread_.joinable()) {
            std::atomic<bool> joined{false};
            std::thread joiner([this, &joined]() { 
                dispatcher_thread_.join(); 
                joined.store(true); 
            });
            for (int i = 0; i < 30 && !joined.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (joined.load()) {
                joiner.join();
            } else {
                std::cerr << "[BinanceEngine] Dispatcher join timeout, detaching\n";
                joiner.detach();
            }
        }
        
        // Stop symbol threads (they check running_ flag)
        for (auto& thread : symbol_threads_) {
            thread->stop();
        }
        
        // Stop order sender
        order_sender_.stop();
        
        state_ = EngineState::STOPPED;
        printf("[BinanceEngine] STOPPED\n");
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ACCESSORS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] EngineState state() const noexcept { return state_; }
    [[nodiscard]] bool is_running() const noexcept { return state_ == EngineState::RUNNING; }
    
    [[nodiscard]] const SymbolThread* get_symbol_thread(uint16_t symbol_id) const noexcept {
        for (const auto& thread : symbol_threads_) {
            if (thread->config().id == symbol_id) {
                return thread.get();
            }
        }
        return nullptr;
    }
    
    // Stats
    [[nodiscard]] uint64_t total_ticks() const noexcept {
        uint64_t total = 0;
        for (const auto& thread : symbol_threads_) {
            total += thread->tick_count();
        }
        return total;
    }
    
    [[nodiscard]] uint64_t total_trades() const noexcept {
        uint64_t total = 0;
        for (const auto& thread : symbol_threads_) {
            total += thread->trade_count();
        }
        return total;
    }
    
    [[nodiscard]] uint64_t orders_sent() const noexcept { return order_sender_.orders_sent(); }
    [[nodiscard]] uint64_t orders_filled() const noexcept { return order_sender_.orders_filled(); }
    
    // Connection status
    [[nodiscard]] bool isConnected() const noexcept { return market_ws_.is_connected(); }
    
    // v4.9.29: Retry time sync (for probe recovery)
    bool syncServerTime(uint64_t& server_time_ms) noexcept {
        if (rest_client_.get_server_time(server_time_ms)) {
            Binance::BinanceTimeSync::set_offset(static_cast<int64_t>(server_time_ms));
            Binance::BinanceTimeSync::mark_initialized();
            return true;
        }
        return false;
    }
    
    // v4.7.0: Intent state for ExecutionAuthority
    void setIntentLive(bool live) noexcept { order_sender_.setIntentLive(live); }
    bool isIntentLive() const noexcept { return order_sender_.isIntentLive(); }
    
    // v6.97: PnL stats
    [[nodiscard]] double total_realized_pnl() const noexcept {
        double total = 0.0;
        for (const auto& [id, tracker] : position_trackers_) {
            total += tracker.realized_pnl;
        }
        return total;
    }
    
    [[nodiscard]] uint32_t total_wins() const noexcept {
        uint32_t total = 0;
        for (const auto& [id, tracker] : position_trackers_) {
            total += tracker.wins;
        }
        return total;
    }
    
    [[nodiscard]] uint32_t total_losses() const noexcept {
        uint32_t total = 0;
        for (const auto& [id, tracker] : position_trackers_) {
            total += tracker.losses;
        }
        return total;
    }
    
    // Latency stats (network latency from Binance event_time to local processing)
    [[nodiscard]] double avg_latency_ms() const noexcept {
        uint64_t count = latency_count_.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(total_latency_ms_.load(std::memory_order_relaxed)) / count;
    }
    [[nodiscard]] uint64_t max_latency_ms() const noexcept {
        return max_latency_ms_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t min_latency_ms() const noexcept {
        return min_latency_ms_.load(std::memory_order_relaxed);
    }
    // v6.97: Current latency (last message)
    [[nodiscard]] uint64_t current_latency_ms() const noexcept {
        return current_latency_ms_.load(std::memory_order_relaxed);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.10: HOT-PATH ORDER LATENCY (send → ACK)
    // This is the REAL latency that matters for trading
    // ═══════════════════════════════════════════════════════════════════════
    
    // Get full latency snapshot for LatencyGate integration
    [[nodiscard]] Chimera::HotPathLatencyTracker::LatencySnapshot hotPathLatencySnapshot() const noexcept {
        return order_sender_.latencySnapshot();
    }
    
    // Individual metrics (milliseconds) - for GUI display
    [[nodiscard]] double hotPathLatency_min_ms() const noexcept { return order_sender_.latency_min_ms(); }
    [[nodiscard]] double hotPathLatency_p10_ms() const noexcept { return order_sender_.latency_p10_ms(); }
    [[nodiscard]] double hotPathLatency_p50_ms() const noexcept { return order_sender_.latency_p50_ms(); }
    [[nodiscard]] double hotPathLatency_p90_ms() const noexcept { return order_sender_.latency_p90_ms(); }
    [[nodiscard]] double hotPathLatency_p99_ms() const noexcept { return order_sender_.latency_p99_ms(); }
    [[nodiscard]] double hotPathLatency_max_ms() const noexcept { return order_sender_.latency_max_ms(); }
    
    // Sample counts
    [[nodiscard]] uint64_t hotPathLatency_samples() const noexcept { return order_sender_.latency_samples(); }
    [[nodiscard]] uint64_t hotPathLatency_spikesFiltered() const noexcept { return order_sender_.latency_spikes_filtered(); }
    
    // Reset latency stats (e.g., on reconnect)
    void resetHotPathLatencyStats() noexcept { order_sender_.resetLatencyStats(); }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.2.2: SHADOW TRADE CALLBACK
    // ═══════════════════════════════════════════════════════════════════════
    using ShadowTradeCallback = std::function<void(const char* symbol, int8_t side, double qty, double price, double pnl_bps)>;
    
    void setShadowTradeCallback(ShadowTradeCallback cb) {
        shadow_trade_callback_ = std::move(cb);
        // Pass to all symbol threads
        for (auto& thread : symbol_threads_) {
            thread->setShadowTradeCallback(shadow_trade_callback_);
        }
    }
    
    // v4.5.0: Tick callback for GUI updates
    void setTickCallback(TickCallback cb) {
        tick_callback_ = std::move(cb);
    }
    
    // v4.9.7: External fill callback for MicroScalp integration
    void setExternalFillCallback(ExternalFillCallback cb) {
        external_fill_callback_ = std::move(cb);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.1: EXTERNAL ORDER SUBMISSION (for MicroScalp engine)
    // ═══════════════════════════════════════════════════════════════════════
    // Submit order via WebSocket (NOT REST - preserves 0.2ms edge)
    // v4.9.8: Added price parameter for LIMIT orders (maker)
    //   price > 0: LIMIT order (maker fee)
    //   price = 0: MARKET order (taker fee)
    bool submitOrder(const char* sym, bool is_buy, double qty, double price = 0.0) {
        // Find symbol ID
        uint16_t sym_id = 0;
        for (size_t i = 0; i < NUM_SYMBOLS; ++i) {
            if (std::strcmp(SYMBOLS[i].symbol, sym) == 0) {
                sym_id = static_cast<uint16_t>(i);
                break;
            }
        }
        
        // Create order intent
        OrderIntent intent;
        intent.symbol_id = sym_id;
        intent.side = is_buy ? Side::Buy : Side::Sell;
        intent.quantity = qty;
        intent.price = price;  // v4.9.8: Now passed through (0=market, >0=limit)
        intent.ts_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        intent.strategy_id = 3;  // CRYPTO_MICROSCALP
        
        // Push to queue (WebSocket sender will process)
        return order_queue_.push(intent);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.10: BOOTSTRAP PROBE ORDERS
    // v4.9.27: Added use_ioc parameter for safe IOC probes
    // ═══════════════════════════════════════════════════════════════════════
    
    // Send a probe order for latency measurement (far from market, will be cancelled)
    bool sendProbeOrder(uint16_t symbol_id, const char* symbol, double price, double qty, 
                        uint64_t client_order_id, bool use_ioc = false) {
        return order_sender_.send_probe_order(symbol_id, symbol, price, qty, client_order_id, use_ioc);
    }
    
    // Cancel a probe order after ACK
    void cancelProbeOrder(uint16_t symbol_id, const char* symbol, uint64_t exchange_order_id) {
        order_sender_.cancel_probe_order(symbol_id, symbol, exchange_order_id);
    }
    
    // Set probe callbacks
    void setProbeAckCallback(ProbeAckCallback cb) {
        order_sender_.setProbeAckCallback(std::move(cb));
    }
    
    void setProbeCancelAckCallback(ProbeCancelAckCallback cb) {
        order_sender_.setProbeCancelAckCallback(std::move(cb));
    }
    
    void setProbeRejectCallback(ProbeRejectCallback cb) {
        order_sender_.setProbeRejectCallback(std::move(cb));
    }
    
    // Probe stats
    uint64_t probesSent() const noexcept { return order_sender_.probes_sent(); }
    uint64_t probesAcked() const noexcept { return order_sender_.probes_acked(); }
    uint64_t probesCancelled() const noexcept { return order_sender_.probes_cancelled(); }
    
    // Check if system is in bootstrap mode
    bool isBootstrapMode() const noexcept {
        return Chimera::getSystemMode().isBootstrap();
    }
    
    // Check if system is live
    bool isLiveMode() const noexcept {
        return Chimera::getSystemMode().isLive();
    }

    // v4.9.34: Bootstrap probe notification forwarding
    void notifyBootstrapProbeSent(uint16_t symbol_id, uint64_t order_id, uint64_t sent_ns) noexcept {
        for (auto& thread : symbol_threads_) {
            if (thread && thread->config().id == symbol_id) {
                thread->notify_probe_sent(order_id, sent_ns);
                return;
            }
        }
    }

    void notifyBootstrapProbeAck(uint16_t symbol_id, uint64_t order_id, uint64_t ack_ns, uint64_t latency_ns) noexcept {
        for (auto& thread : symbol_threads_) {
            if (thread && thread->config().id == symbol_id) {
                thread->notify_probe_ack(order_id, ack_ns, latency_ns);
                printf("[ENGINE] ✓ Bootstrap notified for symbol_id=%u latency=%.3fms\n", 
                       symbol_id, latency_ns / 1'000'000.0);
                return;
            }
        }
    }

    void notifyBootstrapProbeReject(uint16_t symbol_id, uint64_t order_id, int error_code, const char* reason) noexcept {
        for (auto& thread : symbol_threads_) {
            if (thread && thread->config().id == symbol_id) {
                thread->notify_probe_reject(order_id, error_code, reason);
                return;
            }
        }
    }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // DISPATCHER LOOP
    // ═══════════════════════════════════════════════════════════════════════
    
    void dispatcher_loop() noexcept {
        BinanceParser parser;
        uint64_t msg_count = 0;
        uint64_t depth_count = 0;
        uint64_t trade_count = 0;
        
        std::cout << "[BINANCE] Dispatcher started\n";
        
        while (running_.load(std::memory_order_relaxed)) {
            // Check global kill
            if (global_kill_.killed()) {
                std::cout << "[BINANCE] Global kill triggered\n";
                break;
            }
            
            // Check connection
            if (!market_ws_.is_connected()) {
                std::cout << "[BINANCE] WebSocket disconnected, reconnecting...\n";
                // Try to reconnect
                if (!market_ws_.reconnect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                std::cout << "[BINANCE] Reconnected\n";
            }
            
            // Poll for messages
            int count = market_ws_.poll([this, &parser, &msg_count, &depth_count, &trade_count](const char* data, size_t len, WSOpcode opcode) {
                if (opcode == WSOpcode::TEXT) {
                    msg_count++;
                    
                    // v4.2.2: Debug first few raw messages
                    static uint64_t raw_log_count = 0;
                    if (raw_log_count < 3) {
                        std::cout << "[WS-RAW] len=" << len << " data=" << std::string(data, std::min(len, (size_t)200)) << "\n";
                        raw_log_count++;
                    }
                    
                    MessageType type = parser.parse(data, len);
                    
                    if (type == MessageType::DEPTH_UPDATE) {
                        depth_count++;
                        DepthUpdate update;
                        if (parser.parse_depth(update)) {
                            // v6.99: Partial book depth has no event_time
                            // For latency, use trade messages or estimate from receive time
                            if (!update.is_partial_book && update.event_time > 0) {
                                record_latency(update.event_time);
                            }
                            
                            // v3.6-clean: Removed DEPTH-DBG logging
                            
                            uint16_t sym_id = symbol_to_id(update.symbol, update.symbol_len);
                            for (auto& thread : symbol_threads_) {
                                if (thread->config().id == sym_id) {
                                    thread->on_depth(update);
                                    break;
                                }
                            }
                        } else {
                            // Parse failed
                        }
                    } else if (type == MessageType::TRADE) {
                        trade_count++;
                        TradeUpdate trade;
                        if (parser.parse_trade(trade)) {
                            uint16_t sym_id = symbol_to_id(trade.symbol, trade.symbol_len);
                            for (auto& thread : symbol_threads_) {
                                if (thread->config().id == sym_id) {
                                    thread->on_trade(trade);
                                    break;
                                }
                            }
                        }
                    } else if (type == MessageType::BOOK_TICKER) {
                        // v7.12: Real-time best bid/ask - FASTEST stream!
                        BookTickerUpdate ticker;
                        if (parser.parse_book_ticker(ticker)) {
                            uint16_t sym_id = symbol_to_id(ticker.symbol, ticker.symbol_len);
                            for (auto& thread : symbol_threads_) {
                                if (thread->config().id == sym_id) {
                                    thread->on_book_ticker(ticker);
                                    
                                    // v4.5.0: Fire tick callback for GUI updates
                                    if (tick_callback_) {
                                        double lat_ms = static_cast<double>(current_latency_ms_.load(std::memory_order_relaxed));
                                        tick_callback_(thread->config().symbol, 
                                                      ticker.best_bid, ticker.best_ask,
                                                      ticker.best_bid_qty, ticker.best_ask_qty, lat_ms);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    
                    // v3.6-clean: Removed verbose per-1000-msg logging
                }
            });
            
            if (count == 0) {
                // No messages - brief sleep
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        std::cout << "[BINANCE] Dispatcher stopped\n";
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // INITIAL SNAPSHOT FETCH (uses REST - cold path only)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool fetch_initial_snapshots() noexcept {
        // v6.97: With @depth20@100ms we get FULL snapshots on the stream
        // No REST API call needed! The first stream message gives us full book.
        // Just mark all symbols as ready to receive stream data.
        
        for (auto& thread : symbol_threads_) {
            // Create empty snapshot - will be populated by first stream message
            DepthUpdate empty;
            empty.event_time = 0;
            empty.first_update_id = 0;
            empty.last_update_id = 0;
            empty.bid_count = 0;
            empty.ask_count = 0;
            empty.symbol = thread->config().symbol;
            empty.symbol_len = strlen(thread->config().symbol);
            
            thread->set_snapshot(empty);
        }
        
        return true;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ORDER CALLBACKS - v6.97: REAL PnL TRACKING
    // ═══════════════════════════════════════════════════════════════════════
    
    void on_fill(uint16_t symbol_id, Side side, double qty, double price) noexcept {
        // v6.97 FIX: Calculate REAL PnL using position tracker
        auto it = position_trackers_.find(symbol_id);
        if (it == position_trackers_.end()) {
            position_trackers_[symbol_id] = PositionTracker{};
            it = position_trackers_.find(symbol_id);
        }
        
        double pnl = it->second.on_fill(side, qty, price);
        
        // Update daily loss guard with actual PnL
        if (pnl != 0.0) {
            daily_loss_.on_fill(pnl);
        }
        
        // Get symbol name for logging
        const char* sym_name = "UNKNOWN";
        for (const auto& s : SYMBOLS) {
            if (s.id == symbol_id) {
                sym_name = s.symbol;
                break;
            }
        }
        
        // v4.9.7: Notify external MicroScalp engines of fill
        if (external_fill_callback_) {
            external_fill_callback_(sym_name, side == Side::Buy, qty, price);
        }
        
        // Log fill with PnL
        if (pnl != 0.0) {
            printf("[FILL] %s %s Qty=%.6f Price=%.2f -> PnL=$%.4f (total: W=%u L=%u)\n",
                   sym_name,
                   side == Side::Buy ? "BUY" : "SELL",
                   qty, price, pnl,
                   it->second.wins, it->second.losses);
        } else {
            printf("[FILL] %s %s Qty=%.6f Price=%.2f (position: %.6f @ %.2f)\n",
                   sym_name,
                   side == Side::Buy ? "BUY" : "SELL",
                   qty, price,
                   it->second.quantity, it->second.avg_entry_price);
        }
    }
    
    void on_reject(uint16_t symbol_id, const char* reason) noexcept {
        // Get symbol name for logging
        const char* sym_name = "UNKNOWN";
        for (const auto& s : SYMBOLS) {
            if (s.id == symbol_id) {
                sym_name = s.symbol;
                break;
            }
        }
        printf("[REJECT] %s Reason=%s\n", sym_name, reason);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER DATA
    // ═══════════════════════════════════════════════════════════════════════
    
    // Shared state
    GlobalKill& global_kill_;
    DailyLossGuard& daily_loss_;
    
    // Configuration
    const Config config_;
    
    // v4.9.13: REST client for server time sync
    BinanceRestClient rest_client_;
    
    // State
    EngineState state_;
    std::atomic<bool> running_;
    
    // Market data WebSocket (shared by all symbols)
    WebSocketConnection market_ws_;
    
    // Dispatcher thread
    std::thread dispatcher_thread_;
    
    // Order queue and sender
    OrderQueue<256> order_queue_;
    OrderSender order_sender_;
    
    // Symbol threads (one per symbol)
    std::array<std::unique_ptr<SymbolThread>, NUM_SYMBOLS> symbol_threads_;
    
    // v6.97: Position trackers for PnL calculation
    std::unordered_map<uint16_t, PositionTracker> position_trackers_;
    
    // Latency tracking (network latency from Binance event_time)
    std::atomic<uint64_t> total_latency_ms_{0};
    std::atomic<uint64_t> max_latency_ms_{0};
    std::atomic<uint64_t> min_latency_ms_{UINT64_MAX};
    std::atomic<uint64_t> latency_count_{0};
    std::atomic<uint64_t> current_latency_ms_{0};  // v6.97: Track current/last latency
    
    // Record a latency sample
    void record_latency(uint64_t event_time_ms) noexcept {
        uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (event_time_ms > 0 && now_ms > event_time_ms) {
            uint64_t latency = now_ms - event_time_ms;
            // Sanity check - ignore if > 10 seconds (clock skew)
            if (latency < 10000) {
                // v6.97: Store current latency for UI
                current_latency_ms_.store(latency, std::memory_order_relaxed);
                
                total_latency_ms_.fetch_add(latency, std::memory_order_relaxed);
                latency_count_.fetch_add(1, std::memory_order_relaxed);
                
                // Update max
                uint64_t cur_max = max_latency_ms_.load(std::memory_order_relaxed);
                while (latency > cur_max && !max_latency_ms_.compare_exchange_weak(cur_max, latency)) {}
                
                // Update min
                uint64_t cur_min = min_latency_ms_.load(std::memory_order_relaxed);
                while (latency < cur_min && !min_latency_ms_.compare_exchange_weak(cur_min, latency)) {}
            }
        }
    }
    
    // v4.2.2: Shadow trade callback
    ShadowTradeCallback shadow_trade_callback_;
    
    // v4.5.0: Tick callback for GUI updates
    TickCallback tick_callback_;
    
    // v4.9.7: External fill callback for MicroScalp engines
    ExternalFillCallback external_fill_callback_;
};

} // namespace Binance
} // namespace Chimera
