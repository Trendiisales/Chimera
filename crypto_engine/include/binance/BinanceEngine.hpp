// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceEngine.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: Main Binance trading engine - owns all symbol threads and connections
// OWNER: Jo
// LAST VERIFIED: 2024-12-24
//
// v6.97 FIXES:
//   - Implemented REAL PnL tracking (was TODO with pnl=0.0)
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
#include <atomic>
#include <unordered_map>

#include "BinanceConfig.hpp"
#include "BinanceWebSocket.hpp"
#include "BinanceParser.hpp"
#include "SymbolThread.hpp"
#include "BinanceOrderSender.hpp"

#include "../core/GlobalKill.hpp"
#include "../risk/DailyLossGuard.hpp"

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
// Binance Engine
// ─────────────────────────────────────────────────────────────────────────────
class BinanceEngine {
public:
    BinanceEngine(GlobalKill& global_kill, DailyLossGuard& daily_loss) noexcept
        : global_kill_(global_kill)
        , daily_loss_(daily_loss)
        , config_(get_config())
        , state_(EngineState::STOPPED)
        , running_(false)
        , order_sender_(order_queue_, global_kill, config_)
    {
        // Create symbol threads
        for (size_t i = 0; i < NUM_SYMBOLS; ++i) {
            symbol_threads_[i] = std::make_unique<SymbolThread>(
                SYMBOLS[i], global_kill, daily_loss, order_queue_
            );
            // v6.97: Initialize position trackers
            position_trackers_[SYMBOLS[i].id] = PositionTracker{};
        }
        
        // Set up order sender callbacks
        order_sender_.set_on_fill([this](uint16_t sym_id, Side side, double qty, double price) {
            on_fill(sym_id, side, qty, price);
        });
        
        order_sender_.set_on_reject([this](uint16_t sym_id, const char* reason) {
            on_reject(sym_id, reason);
        });
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
        
        // Stop dispatcher
        if (dispatcher_thread_.joinable()) {
            dispatcher_thread_.join();
        }
        
        // Stop symbol threads
        for (auto& thread : symbol_threads_) {
            thread->stop();
        }
        
        // Stop order sender
        order_sender_.stop();
        
        // Disconnect WebSockets
        market_ws_.disconnect();
        
        state_ = EngineState::STOPPED;
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

private:
    // ═══════════════════════════════════════════════════════════════════════
    // DISPATCHER LOOP
    // ═══════════════════════════════════════════════════════════════════════
    
    void dispatcher_loop() noexcept {
        BinanceParser parser;
        uint64_t msg_count = 0;
        uint64_t depth_count = 0;
        uint64_t trade_count = 0;
        
        std::cout << "[BINANCE-DISP] Dispatcher loop started\n";
        
        while (running_.load(std::memory_order_relaxed)) {
            // Check global kill
            if (global_kill_.killed()) {
                std::cout << "[BINANCE-DISP] Global kill triggered\n";
                break;
            }
            
            // Check connection
            if (!market_ws_.is_connected()) {
                std::cout << "[BINANCE-DISP] WebSocket disconnected, reconnecting...\n";
                // Try to reconnect
                if (!market_ws_.reconnect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                std::cout << "[BINANCE-DISP] Reconnected\n";
            }
            
            // Poll for messages
            int count = market_ws_.poll([this, &parser, &msg_count, &depth_count, &trade_count](const char* data, size_t len, WSOpcode opcode) {
                if (opcode == WSOpcode::TEXT) {
                    msg_count++;
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
                            
                            // DEBUG: Show first few depth updates
                            if (depth_count <= 5) {
                                std::cout << "[DEPTH-DBG] #" << depth_count
                                          << " sym=" << std::string(update.symbol, update.symbol_len)
                                          << " partial=" << (update.is_partial_book ? "YES" : "NO")
                                          << " bids=" << (int)update.bid_count
                                          << " asks=" << (int)update.ask_count;
                                if (update.bid_count > 0) {
                                    std::cout << " bid[0]=" << update.bids[0].price;
                                }
                                if (update.ask_count > 0) {
                                    std::cout << " ask[0]=" << update.asks[0].price;
                                }
                                std::cout << "\n";
                            }
                            
                            uint16_t sym_id = symbol_to_id(update.symbol, update.symbol_len);
                            for (auto& thread : symbol_threads_) {
                                if (thread->config().id == sym_id) {
                                    thread->on_depth(update);
                                    
                                    // DEBUG: Show book state after update
                                    if (depth_count <= 5) {
                                        const auto& book = thread->book();
                                        std::cout << "[BOOK-DBG] " << thread->config().symbol
                                                  << " bid=" << book.best_bid()
                                                  << " ask=" << book.best_ask()
                                                  << " spread=" << book.spread_bps() << "bps"
                                                  << " levels=" << (int)book.bid_levels() << "/" << (int)book.ask_levels()
                                                  << " valid=" << (book.valid() ? "YES" : "NO")
                                                  << " state=" << static_cast<int>(thread->state()) << "\n";
                                    }
                                    break;
                                }
                            }
                        } else {
                            // Parse failed
                            if (depth_count <= 3) {
                                std::cout << "[DEPTH-FAIL] parse_depth returned false\n";
                            }
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
                    }
                    
                    // Debug every 1000 messages
                    if (msg_count % 1000 == 0) {
                        // Show book validity for all symbols
                        std::cout << "[BINANCE-DISP] msgs=" << msg_count 
                                  << " depth=" << depth_count 
                                  << " trades=" << trade_count 
                                  << " ticks=" << total_ticks()
                                  << " lat=" << current_latency_ms_.load() << "ms";
                        for (const auto& t : symbol_threads_) {
                            std::cout << " | " << t->config().symbol 
                                      << ":" << (t->book().valid() ? "OK" : "STALE");
                        }
                        std::cout << "\n";
                    }
                }
            });
            
            if (count == 0) {
                // No messages - brief sleep
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        std::cout << "[BINANCE-DISP] Dispatcher loop exited\n";
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
};

} // namespace Binance
} // namespace Chimera
