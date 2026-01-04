// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceOrderSender.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: WebSocket API order execution for Binance
// OWNER: Jo
// LAST VERIFIED: 2026-01-03
//
// v4.9.23: EXECUTION QUALITY TELEMETRY + VENUE FAILOVER
// ═══════════════════════════════════════════════════════════════════════════════
//   LAYER A: ExecutionQuality - per-symbol stats (sent/acked/rejected/latency)
//   LAYER B: ExecutionGovernor - auto venue degradation/halt on issues
//   LAYER C: ExecutionCostModel - alpha filtering by net edge after costs
//   
//   Also: Proper select()/poll() FD gating before SSL_read()
//
// v4.9.22: CRITICAL FIX - RESPONSE ROUTING
// ═══════════════════════════════════════════════════════════════════════════════
//   ROOT CAUSE: Orders were sent successfully but responses were DROPPED because:
//     1. Socket was BLOCKING - SSL_read() waited forever, poll() never returned
//     2. Response matching used symbol_id instead of Binance WS "id" field
//   
//   FIXES:
//     - Socket set to NON-BLOCKING mode after handshake
//     - Parse "id" field from all responses for correct matching
//     - Pending probes keyed by request_id (not client_order_id)
//     - Enhanced logging for response reception
//
// v4.9.10: HONEST LATENCY TRACKING
//   - Integrated HotPathLatencyTracker for send→ACK latency
//   - Records latency on FILL/NEW responses only (not reconnects)
//   - Exposes min/p10/p50 metrics for GUI
//   - Filters >5ms spikes (reconnect artifacts)
//
// v4.9.5: MAKER-FIRST ROUTING
//   - Post-only limit order first
//   - Wait for maker_timeout_ms
//   - If not filled, cancel and send market order
//   - Track fill types for fee attribution
//
// DESIGN:
// - Dedicated thread for order sending
// - Consumes from lock-free order queue
// - Uses WebSocket API (NOT REST) for minimum latency
// - HMAC-SHA256 signed requests
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <unordered_map>

#include "BinanceConfig.hpp"
#include "BinanceWebSocket.hpp"
#include "BinanceHMAC.hpp"
#include "BinanceParser.hpp"
#include "SymbolThread.hpp"

#include "shared/GlobalRiskGovernor.hpp"
#include "core/EngineOwnership.hpp"
#include "core/ExecutionAuthority.hpp"
#include "latency/HotPathLatencyTracker.hpp"  // v4.9.10: Honest latency tracking

// v4.9.23: Execution quality layers
#include "execution/ExecutionQuality.hpp"
#include "execution/ExecutionGovernor.hpp"
#include "execution/ExecutionCostModel.hpp"

namespace Chimera {
namespace Binance {

// ─────────────────────────────────────────────────────────────────────────────
// Fill Type for fee attribution
// ─────────────────────────────────────────────────────────────────────────────
enum class FillType : uint8_t {
    UNKNOWN = 0,
    MAKER   = 1,
    TAKER   = 2
};

// ─────────────────────────────────────────────────────────────────────────────
// Pending Order Tracking (for maker-first flow)
// ─────────────────────────────────────────────────────────────────────────────
struct PendingOrder {
    uint16_t symbol_id;
    Side side;
    double quantity;
    double limit_price;
    uint64_t send_ts_ns;
    uint64_t order_id;           // Exchange order ID
    bool is_maker_attempt;
    bool filled;
    double filled_price;
    FillType fill_type;
};

// ─────────────────────────────────────────────────────────────────────────────
// v4.9.22: Probe Order Tracking (for latency bootstrap)
// Now keyed by request_id for proper response matching
// ─────────────────────────────────────────────────────────────────────────────
struct ProbeOrder {
    uint64_t request_id;          // v4.9.22: Binance WS "id" for matching
    uint64_t client_order_id;
    uint64_t exchange_order_id;
    uint16_t symbol_id;
    uint64_t send_ts_ns;
    uint64_t ack_ts_ns;
    uint64_t cancel_send_ts_ns;
    uint64_t cancel_ack_ts_ns;
    bool acked;
    bool cancel_sent;
    bool cancelled;
};

// Probe callbacks for bootstrapper integration
using ProbeAckCallback = std::function<void(uint16_t symbol_id, uint64_t client_order_id, uint64_t exchange_order_id, uint64_t latency_ns)>;
using ProbeCancelAckCallback = std::function<void(uint64_t exchange_order_id)>;
using ProbeRejectCallback = std::function<void(uint64_t client_order_id)>;

// ─────────────────────────────────────────────────────────────────────────────
// Order Response  
// v4.9.22: Added request_id for proper response routing by Binance WS "id"
// ─────────────────────────────────────────────────────────────────────────────
struct OrderResponse {
    uint64_t request_id;        // v4.9.22: Binance WS "id" field for matching
    int      http_status;       // v4.9.22: Outer "status" (200, 400, etc)
    uint64_t order_id;
    uint64_t client_order_id;
    uint16_t symbol_id;
    double   executed_qty;
    double   executed_price;
    bool     success;
    char     status[16];
    char     error_msg[128];
};

// ─────────────────────────────────────────────────────────────────────────────
// Order Sender Thread
// ─────────────────────────────────────────────────────────────────────────────
class OrderSender {
public:
    using OnFill = std::function<void(uint16_t symbol_id, Side side, double qty, double price, FillType fill_type)>;
    using OnReject = std::function<void(uint16_t symbol_id, const char* reason)>;
    using OnMakerTimeout = std::function<void(uint16_t symbol_id)>;
    
    OrderSender(
        OrderQueue<256>& order_queue,
        GlobalKill& global_kill,
        const Config& config
    ) noexcept
        : order_queue_(order_queue)
        , global_kill_(global_kill)
        , config_(config)
        , signer_(config.secret_key)
        , running_(false)
        , orders_sent_(0)
        , orders_filled_(0)
        , orders_rejected_(0)
        , blocked_orders_(0)
        , paper_fills_(0)
        , maker_fills_(0)
        , taker_fills_(0)
        , maker_timeouts_(0)
        , intent_is_live_(false)
        , maker_timeout_ms_(40)  // Default 40ms
        , latency_tracker_(256, 500'000'000ULL)  // v4.9.34: 256 samples, 500ms spike filter (London→Tokyo)
    {}
    
    ~OrderSender() {
        stop();
    }
    
    // Set callbacks
    void set_on_fill(OnFill cb) noexcept { on_fill_ = cb; }
    void set_on_reject(OnReject cb) noexcept { on_reject_ = cb; }
    void set_on_maker_timeout(OnMakerTimeout cb) noexcept { on_maker_timeout_ = cb; }
    
    void setIntentLive(bool live) noexcept { intent_is_live_.store(live, std::memory_order_release); }
    bool isIntentLive() const noexcept { return intent_is_live_.load(std::memory_order_acquire); }
    
    void setMakerTimeout(uint32_t ms) noexcept { maker_timeout_ms_ = ms; }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.10: HOT-PATH LATENCY METRICS (send → ACK)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Get latency snapshot for GUI (thread-safe)
    [[nodiscard]] Chimera::HotPathLatencyTracker::LatencySnapshot latencySnapshot() const noexcept {
        return latency_tracker_.snapshot();
    }
    
    // Individual metrics (milliseconds for GUI display)
    [[nodiscard]] double latency_min_ms() const noexcept { return latency_tracker_.min_ms(); }
    [[nodiscard]] double latency_p10_ms() const noexcept { return latency_tracker_.p10_ms(); }
    [[nodiscard]] double latency_p50_ms() const noexcept { return latency_tracker_.p50_ms(); }
    [[nodiscard]] double latency_p90_ms() const noexcept { return latency_tracker_.p90_ms(); }
    [[nodiscard]] double latency_p99_ms() const noexcept { return latency_tracker_.p99_ms(); }
    [[nodiscard]] double latency_max_ms() const noexcept { return latency_tracker_.max_ms(); }
    
    // Nanosecond metrics for internal use
    [[nodiscard]] uint64_t latency_min_ns() const noexcept { return latency_tracker_.min_ns(); }
    [[nodiscard]] uint64_t latency_p10_ns() const noexcept { return latency_tracker_.p10_ns(); }
    [[nodiscard]] uint64_t latency_p50_ns() const noexcept { return latency_tracker_.p50_ns(); }
    [[nodiscard]] uint64_t latency_p90_ns() const noexcept { return latency_tracker_.p90_ns(); }
    [[nodiscard]] uint64_t latency_p99_ns() const noexcept { return latency_tracker_.p99_ns(); }
    
    // Latency sample counts
    [[nodiscard]] uint64_t latency_samples() const noexcept { return latency_tracker_.total_recorded(); }
    [[nodiscard]] uint64_t latency_spikes_filtered() const noexcept { return latency_tracker_.spikes_filtered(); }
    
    // Reset latency stats (e.g., on reconnect)
    void resetLatencyStats() noexcept { latency_tracker_.reset(); }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool start() noexcept {
        if (running_.load()) return true;
        
        if (config_.api_key == nullptr || config_.secret_key == nullptr) {
            std::cerr << "[OrderSender] No API keys configured - trading disabled\n";
            return false;
        }
        
        // v4.9.19: CRITICAL STARTUP DIAGNOSTIC
        printf("\n");
        printf("╔══════════════════════════════════════════════════════════════╗\n");
        printf("║  ORDER SENDER STARTUP - v4.9.19 DIAGNOSTIC                   ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║  WebSocket Trading API Endpoint:                             ║\n");
        printf("║    Host: %s\n", config_.ws_api_host);
        printf("║    Port: %u\n", config_.ws_api_port);
        printf("║    Path: %s\n", config_.ws_api_path);
        printf("║  API Key: %.20s...%.4s\n", 
               config_.api_key, config_.api_key + strlen(config_.api_key) - 4);
        printf("║  Time Sync: %s (offset=%+lldms)\n", 
               BinanceTimeSync::is_initialized() ? "✓ READY" : "⏳ PENDING",
               static_cast<long long>(BinanceTimeSync::offset_ms()));
        printf("╚══════════════════════════════════════════════════════════════╝\n");
        printf("\n");
        
        printf("[ORDER-WS] Connecting to %s:%u%s...\n",
               config_.ws_api_host, config_.ws_api_port, config_.ws_api_path);
        
        if (!ws_.connect(config_.ws_api_host, config_.ws_api_port, config_.ws_api_path)) {
            printf("[ORDER-WS] ✗ Connection FAILED!\n");
            return false;
        }
        
        printf("[ORDER-WS] ✓ Connected to Binance WebSocket Trading API\n");
        
        running_.store(true);
        thread_ = std::thread(&OrderSender::run, this);
        return true;
    }
    
    void stop() noexcept {
        running_.store(false);
        ws_.disconnect();
        if (thread_.joinable()) {
            std::atomic<bool> joined{false};
            std::thread joiner([this, &joined]() { 
                thread_.join(); 
                joined.store(true); 
            });
            for (int i = 0; i < 20 && !joined.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (joined.load()) {
                joiner.join();
            } else {
                std::cerr << "[BinanceOrderSender] Thread join timeout, detaching\n";
                joiner.detach();
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.9: EMERGENCY SHUTDOWN - Cancel all pending orders
    // ═══════════════════════════════════════════════════════════════════════
    
    void cancelAllOpenOrders() noexcept {
        printf("[EMERGENCY] Cancelling all open orders...\n");
        
        // Cancel all pending maker orders
        for (const auto& [symbol_id, order] : pending_maker_orders_) {
            if (order.order_id != 0) {
                const char* symbol = get_symbol_name(symbol_id);
                if (symbol) {
                    printf("[EMERGENCY] Cancelling MAKER order: %s order_id=%llu\n", 
                           symbol, (unsigned long long)order.order_id);
                    send_cancel_order(symbol, order.order_id);
                }
            }
        }
        
        // Cancel all pending taker orders
        for (const auto& [symbol_id, order] : pending_orders_) {
            if (order.order_id != 0) {
                const char* symbol = get_symbol_name(symbol_id);
                if (symbol) {
                    printf("[EMERGENCY] Cancelling TAKER order: %s order_id=%llu\n",
                           symbol, (unsigned long long)order.order_id);
                    send_cancel_order(symbol, order.order_id);
                }
            }
        }
        
        pending_maker_orders_.clear();
        pending_orders_.clear();
        
        printf("[EMERGENCY] All orders cancelled\n");
    }
    
    // Send emergency market order to flatten position
    void sendEmergencyFlatten(const char* symbol, double qty, bool is_long) noexcept {
        if (qty <= 0.0) return;
        if (!ws_.is_connected()) {
            printf("[EMERGENCY] Cannot flatten %s - WebSocket disconnected!\n", symbol);
            return;
        }
        
        Side side = is_long ? Side::Sell : Side::Buy;
        printf("[EMERGENCY] Flattening %s: %s %.6f\n", 
               symbol, is_long ? "SELL" : "BUY", qty);
        
        // Find symbol_id
        uint16_t symbol_id = 0;
        for (size_t i = 0; i < SYMBOLS.size(); ++i) {
            if (strcmp(SYMBOLS[i].symbol, symbol) == 0) {
                symbol_id = static_cast<uint16_t>(SYMBOLS[i].id);
                break;
            }
        }
        
        if (symbol_id == 0) {
            printf("[EMERGENCY] Unknown symbol: %s\n", symbol);
            return;
        }
        
        send_market_order(symbol_id, side, qty);
    }
    
private:
    const char* get_symbol_name(uint16_t symbol_id) const noexcept {
        for (size_t i = 0; i < SYMBOLS.size(); ++i) {
            if (static_cast<uint16_t>(SYMBOLS[i].id) == symbol_id) {
                return SYMBOLS[i].symbol;
            }
        }
        return nullptr;
    }
    
public:
    // ═══════════════════════════════════════════════════════════════════════
    // STATS
    // ═══════════════════════════════════════════════════════════════════════
    [[nodiscard]] uint64_t orders_sent() const noexcept { return orders_sent_; }
    [[nodiscard]] uint64_t orders_filled() const noexcept { return orders_filled_; }
    [[nodiscard]] uint64_t orders_rejected() const noexcept { return orders_rejected_; }
    [[nodiscard]] uint64_t orders_blocked() const noexcept { return blocked_orders_; }
    [[nodiscard]] uint64_t paper_fills() const noexcept { return paper_fills_; }
    [[nodiscard]] uint64_t maker_fills() const noexcept { return maker_fills_; }
    [[nodiscard]] uint64_t taker_fills() const noexcept { return taker_fills_; }
    [[nodiscard]] uint64_t maker_timeouts() const noexcept { return maker_timeouts_; }
    [[nodiscard]] bool is_connected() const noexcept { return ws_.is_connected(); }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // MAIN LOOP
    // v4.9.23: Integrated with ExecutionGovernor for venue failover
    // ═══════════════════════════════════════════════════════════════════════
    
    void run() noexcept {
        bool was_connected = false;
        
        while (running_.load(std::memory_order_relaxed)) {
            if (global_kill_.killed()) {
                break;
            }
            
            if (!ws_.is_connected()) {
                // v4.9.23: Notify governor of connection loss
                if (was_connected) {
                    ExecutionGovernor::instance().on_connection_lost();
                    was_connected = false;
                }
                
                if (!ws_.reconnect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                
                // v4.9.23: Notify governor of connection restored
                ExecutionGovernor::instance().on_connection_restored();
                was_connected = true;
            }
            
            // Poll for responses (with proper select/poll gating)
            (void)ws_.poll([this](const char* data, size_t len, WSOpcode opcode) {
                if (opcode == WSOpcode::TEXT) {
                    handle_response(data, len);
                }
            });
            
            // Check for maker order timeouts
            check_maker_timeouts();
            
            // v4.9.23: Check venue state before processing orders
            if (ExecutionGovernor::instance().is_halted()) {
                // Don't process orders when halted - just drain queue
                OrderIntent intent;
                if (order_queue_.pop(intent)) {
                    printf("[ORDER_BLOCKED] Venue HALTED - order discarded\n");
                    ++blocked_orders_;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // Process new orders
            OrderIntent intent;
            if (order_queue_.pop(intent)) {
                send_order(intent);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MAKER TIMEOUT HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    
    void check_maker_timeouts() noexcept {
        uint64_t now_ns = get_timestamp_ns();
        uint64_t timeout_ns = static_cast<uint64_t>(maker_timeout_ms_) * 1'000'000ULL;
        
        for (auto it = pending_maker_orders_.begin(); it != pending_maker_orders_.end(); ) {
            if (!it->second.filled && 
                it->second.is_maker_attempt && 
                now_ns - it->second.send_ts_ns > timeout_ns) {
                
                // MAKER TIMEOUT - Cancel and fall back to taker
                maker_timeouts_++;
                
                const SymbolConfig* sym = find_symbol(it->second.symbol_id);
                const char* sym_name = sym ? sym->symbol : "UNKNOWN";
                
                printf("[MAKER_TIMEOUT] %s order_id=%llu age=%llums - cancelling and falling back to TAKER\n",
                       sym_name,
                       static_cast<unsigned long long>(it->second.order_id),
                       static_cast<unsigned long long>((now_ns - it->second.send_ts_ns) / 1'000'000));
                
                // Cancel the maker order
                if (sym) {
                    send_cancel_order(sym->symbol, it->second.order_id);
                }
                
                // Notify callback for potential abort decision
                if (on_maker_timeout_) {
                    on_maker_timeout_(it->second.symbol_id);
                }
                
                // Send taker fallback
                send_market_order(it->second.symbol_id, it->second.side, it->second.quantity);
                
                it = pending_maker_orders_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ORDER SENDING
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] static double normalize_qty(double qty, double minQty, double stepSize) noexcept {
        if (qty < minQty) qty = minQty;
        double steps = std::floor(qty / stepSize);
        double norm = steps * stepSize;
        if (norm < minQty) norm = minQty;
        return norm;
    }
    
    void send_order(const OrderIntent& intent) noexcept {
        const SymbolConfig* sym = find_symbol(intent.symbol_id);
        const char* sym_name = sym ? sym->symbol : "UNKNOWN";
        
        // ═══════════════════════════════════════════════════════════════════
        // EXECUTION AUTHORITY CHECK
        // ═══════════════════════════════════════════════════════════════════
        Chimera::ExecBlockReason block_reason;
        bool intent_live = intent_is_live_.load(std::memory_order_acquire);
        if (!Chimera::getExecutionAuthority().allowCrypto(sym_name, intent_live, &block_reason)) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  ⛔ ORDER BLOCKED - EXECUTION AUTHORITY                   ║\n";
            std::cout << "║  Symbol: " << sym_name << "\n";
            std::cout << "║  Reason: " << Chimera::execBlockReasonToString(block_reason) << "\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++blocked_orders_;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // RISK GOVERNOR CHECK
        // ═══════════════════════════════════════════════════════════════════
        if (!Chimera::GlobalRiskGovernor::instance().canSubmitOrder(Chimera::EngineId::BINANCE)) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  ⛔ ORDER BLOCKED - RISK GOVERNOR                        ║\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++blocked_orders_;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // TRADE MODE CHECKS
        // ═══════════════════════════════════════════════════════════════════
        const char* side = (intent.side == Side::Buy) ? "BUY" : "SELL";
        
        if (!is_trading_logic_enabled()) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  🔒 ORDER BLOCKED - SHADOW MODE                           ║\n";
            std::cout << "║  " << sym_name << " " << side << " qty=" << intent.quantity << "\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++blocked_orders_;
            return;
        }
        
        if (is_paper_mode()) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  📄 PAPER MODE - SIMULATED FILL                          ║\n";
            std::cout << "║  " << sym_name << " " << side << " qty=" << intent.quantity << "\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++paper_fills_;
            if (on_fill_) {
                on_fill_(intent.symbol_id, intent.side, intent.quantity, intent.price, FillType::TAKER);
            }
            return;
        }
        
        if (!is_live_trading_enabled()) {
            std::cout << "[EXEC] ERROR: Reached live execution but is_live_trading_enabled() is false!\n";
            return;
        }
        
        if (!sym) return;
        
        // Zero-qty guard
        if (intent.quantity <= 0.0) {
            std::cout << "[EXEC_BLOCKED] Zero quantity for " << sym->symbol << "\n";
            return;
        }
        
        double final_qty = normalize_qty(intent.quantity, sym->lot_size, sym->lot_size);
        if (final_qty <= 0.0) {
            std::cout << "[EXEC_BLOCKED] Zero normalized quantity for " << sym->symbol << "\n";
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // MAKER-FIRST ROUTING
        // price > 0: MAKER_FIRST (try limit order first)
        // price = 0: TAKER_ONLY (market order)
        // ═══════════════════════════════════════════════════════════════════
        
        if (intent.price > 0.0) {
            // MAKER_FIRST: Send limit order and track for timeout
            send_limit_order(intent.symbol_id, intent.side, final_qty, intent.price, sym);
        } else {
            // TAKER_ONLY: Send market order directly
            send_market_order(intent.symbol_id, intent.side, final_qty);
        }
    }
    
    void send_limit_order(uint16_t symbol_id, Side side, double qty, double price, const SymbolConfig* sym) noexcept {
        // v4.9.13: Block orders until time sync is complete
        if (!Binance::BinanceTimeSync::is_initialized()) {
            printf("[EXEC_BLOCKED] Time sync not complete - order NOT sent\n");
            return;
        }
        
        const char* side_str = (side == Side::Buy) ? "BUY" : "SELL";
        
        char client_order_id[32];
        snprintf(client_order_id, sizeof(client_order_id), "CHM%llu", static_cast<unsigned long long>(get_timestamp_ms()));
        
        WSAPIRequestBuilder builder;
        const char* request = builder.build_new_order(
            sym->symbol,
            side_str,
            "LIMIT",
            qty,
            sym->qty_precision,
            price,
            sym->price_precision,
            "GTC",  // Good-Til-Cancel for maker
            client_order_id,
            signer_,
            config_.api_key
        );
        
        printf("[EXEC_SEND] MAKER_FIRST %s %s %.6f @ %.2f\n", sym->symbol, side_str, qty, price);
        
        // v4.9.10: T1 = IMMEDIATELY before socket write (this is the only correct place)
        uint64_t send_ts_ns = get_timestamp_ns();
        
        if (ws_.send_text(request, builder.length())) {
            ++orders_sent_;
            
            // Track pending maker order
            PendingOrder pending;
            pending.symbol_id = symbol_id;
            pending.side = side;
            pending.quantity = qty;
            pending.limit_price = price;
            pending.send_ts_ns = send_ts_ns;  // Use pre-send timestamp
            pending.order_id = 0;  // Will be set from response
            pending.is_maker_attempt = true;
            pending.filled = false;
            pending.fill_type = FillType::UNKNOWN;
            
            pending_maker_orders_[symbol_id] = pending;
            
            printf("[EXEC_SENT_OK] LIMIT order sent, waiting for fill or timeout (%ums)\n", maker_timeout_ms_);
        } else {
            printf("[EXEC_SEND_FAIL] WebSocket send failed!\n");
        }
    }
    
    void send_market_order(uint16_t symbol_id, Side side, double qty) noexcept {
        // v4.9.13: Block orders until time sync is complete
        if (!Binance::BinanceTimeSync::is_initialized()) {
            printf("[EXEC_BLOCKED] Time sync not complete - order NOT sent\n");
            return;
        }
        
        const SymbolConfig* sym = find_symbol(symbol_id);
        if (!sym) return;
        
        const char* side_str = (side == Side::Buy) ? "BUY" : "SELL";
        
        char client_order_id[32];
        snprintf(client_order_id, sizeof(client_order_id), "CHM%llu", static_cast<unsigned long long>(get_timestamp_ms()));
        
        WSAPIRequestBuilder builder;
        const char* request = builder.build_new_order(
            sym->symbol,
            side_str,
            "MARKET",
            qty,
            sym->qty_precision,
            0.0,
            sym->price_precision,
            "GTC",
            client_order_id,
            signer_,
            config_.api_key
        );
        
        printf("[EXEC_SEND] TAKER %s %s %.6f\n", sym->symbol, side_str, qty);
        
        // v4.9.10: T1 = IMMEDIATELY before socket write (this is the only correct place)
        uint64_t send_ts_ns = get_timestamp_ns();
        
        if (ws_.send_text(request, builder.length())) {
            ++orders_sent_;
            
            // Track as taker order (no timeout needed)
            PendingOrder pending;
            pending.symbol_id = symbol_id;
            pending.side = side;
            pending.quantity = qty;
            pending.limit_price = 0.0;
            pending.send_ts_ns = send_ts_ns;  // Use pre-send timestamp
            pending.order_id = 0;
            pending.is_maker_attempt = false;
            pending.filled = false;
            pending.fill_type = FillType::TAKER;
            
            pending_orders_[symbol_id] = pending;
            
            printf("[EXEC_SENT_OK] MARKET order sent\n");
        } else {
            printf("[EXEC_SEND_FAIL] WebSocket send failed!\n");
        }
    }
    
    void send_cancel_order(const char* symbol, uint64_t order_id) noexcept {
        WSAPIRequestBuilder builder;
        const char* request = builder.build_cancel_order(
            symbol,
            static_cast<int64_t>(order_id),
            signer_,
            config_.api_key
        );
        
        printf("[EXEC_CANCEL] %s order_id=%llu\n", symbol, static_cast<unsigned long long>(order_id));
        
        if (ws_.send_text(request, builder.length())) {
            printf("[EXEC_CANCEL_OK] Cancel request sent\n");
        } else {
            printf("[EXEC_CANCEL_FAIL] WebSocket send failed!\n");
        }
    }
    
public:
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.10: BOOTSTRAP PROBE ORDERS
    // v4.9.12: Added WebSocket health check + alert throttling
    // v4.9.13: Gate on successful time sync (signatures will fail without it)
    // v4.9.27: Added IOC support - use_ioc=true for safe probes
    // Send limit orders far from market to measure ACK latency
    // ═══════════════════════════════════════════════════════════════════════
    
    bool send_probe_order(uint16_t symbol_id, const char* symbol, double price, double qty, 
                          uint64_t client_order_id, bool use_ioc = false) noexcept {
        // v4.9.13: Block probes until time sync is complete
        // Without time sync, signatures WILL fail with -1022
        if (!Binance::BinanceTimeSync::is_initialized()) {
            // Throttle this message
            uint64_t now_ns = get_timestamp_ns();
            static uint64_t last_time_sync_warn_ns = 0;
            if (now_ns - last_time_sync_warn_ns >= 5'000'000'000ULL) {  // 5s throttle
                printf("[PROBE_BLOCKED] Time sync not complete - signatures will fail\n");
                last_time_sync_warn_ns = now_ns;
            }
            return false;
        }
        
        const SymbolConfig* sym = find_symbol(symbol_id);
        if (!sym) {
            printf("[PROBE] Unknown symbol ID: %u\n", symbol_id);
            return false;
        }
        
        // v4.9.12: Check WebSocket connection BEFORE attempting send
        if (!ws_.is_connected()) {
            // Throttle this message - only log every 30 seconds
            uint64_t now_ns = get_timestamp_ns();
            if (now_ns - last_probe_fail_log_ns_ >= PROBE_FAIL_THROTTLE_NS) {
                if (suppressed_probe_fails_ > 0) {
                    printf("[PROBE_SEND_BLOCKED] WebSocket disconnected! (suppressed %llu messages in last 30s)\n",
                           static_cast<unsigned long long>(suppressed_probe_fails_));
                    suppressed_probe_fails_ = 0;
                } else {
                    printf("[PROBE_SEND_BLOCKED] WebSocket disconnected - waiting for reconnect\n");
                }
                last_probe_fail_log_ns_ = now_ns;
            } else {
                suppressed_probe_fails_++;
            }
            return false;
        }
        
        char client_oid_str[32];
        snprintf(client_oid_str, sizeof(client_oid_str), "PRB%llu", static_cast<unsigned long long>(client_order_id));
        
        // v4.9.27: Use IOC (Immediate-Or-Cancel) for safe probes
        // IOC auto-cancels if not filled immediately - perfect for latency probes
        const char* time_in_force = use_ioc ? "IOC" : "GTC";
        
        WSAPIRequestBuilder builder;
        const char* request = builder.build_new_order(
            symbol,
            "BUY",  // Always BUY probes (far below market)
            "LIMIT",
            qty,
            sym->qty_precision,
            price,
            sym->price_precision,
            time_in_force,  // v4.9.27: IOC or GTC based on parameter
            client_oid_str,
            signer_,
            config_.api_key
        );
        
        // v4.9.22: Get request_id AFTER building (it's assigned during build)
        uint64_t request_id = builder.last_request_id();
        
        printf("[PROBE_SEND] %s probe @ %.2f qty=%.6f client_id=%llu request_id=%llu\n", 
               symbol, price, qty, 
               static_cast<unsigned long long>(client_order_id),
               static_cast<unsigned long long>(request_id));
        
        // Record send timestamp BEFORE socket write
        uint64_t send_ts_ns = get_timestamp_ns();
        
        if (ws_.send_text(request, builder.length())) {
            // v4.9.23: Record send in ExecutionQuality telemetry
            ExecutionQuality::instance().record_send(symbol);
            
            // v4.9.22: Track pending probe BY REQUEST_ID (for response matching)
            ProbeOrder probe;
            probe.request_id = request_id;           // v4.9.22: KEY FOR MATCHING
            probe.client_order_id = client_order_id;
            probe.exchange_order_id = 0;
            probe.symbol_id = symbol_id;
            probe.send_ts_ns = send_ts_ns;
            probe.ack_ts_ns = 0;
            probe.cancel_send_ts_ns = 0;
            probe.cancel_ack_ts_ns = 0;
            probe.acked = false;
            probe.cancel_sent = false;
            probe.cancelled = false;
            
            pending_probes_[request_id] = probe;  // v4.9.22: Use request_id as key
            ++probes_sent_;
            
            // Reset throttle state on success
            suppressed_probe_fails_ = 0;
            
            printf("[PROBE_SENT_OK] Probe sent, request_id=%llu waiting for ACK\n",
                   static_cast<unsigned long long>(request_id));
            return true;
        } else {
            // v4.9.12: Throttle failure messages
            uint64_t now_ns = get_timestamp_ns();
            if (now_ns - last_probe_fail_log_ns_ >= PROBE_FAIL_THROTTLE_NS) {
                if (suppressed_probe_fails_ > 0) {
                    printf("[PROBE_SEND_FAIL] WebSocket send failed! (suppressed %llu messages in last 30s)\n",
                           static_cast<unsigned long long>(suppressed_probe_fails_));
                    suppressed_probe_fails_ = 0;
                } else {
                    printf("[PROBE_SEND_FAIL] WebSocket send failed!\n");
                }
                last_probe_fail_log_ns_ = now_ns;
            } else {
                suppressed_probe_fails_++;
            }
            return false;
        }
    }
    
    void cancel_probe_order(uint16_t /*symbol_id*/, const char* symbol, uint64_t exchange_order_id) noexcept {
        WSAPIRequestBuilder builder;
        const char* request = builder.build_cancel_order(
            symbol,
            static_cast<int64_t>(exchange_order_id),
            signer_,
            config_.api_key
        );
        
        // Find probe and record cancel send time
        for (auto& [cid, probe] : pending_probes_) {
            if (probe.exchange_order_id == exchange_order_id && !probe.cancel_sent) {
                probe.cancel_send_ts_ns = get_timestamp_ns();
                probe.cancel_sent = true;
                break;
            }
        }
        
        printf("[PROBE_CANCEL] %s order_id=%llu\n", symbol, static_cast<unsigned long long>(exchange_order_id));
        
        if (ws_.send_text(request, builder.length())) {
            printf("[PROBE_CANCEL_OK] Cancel request sent\n");
        } else {
            printf("[PROBE_CANCEL_FAIL] WebSocket send failed!\n");
        }
    }
    
    // Probe callbacks
    void setProbeAckCallback(ProbeAckCallback cb) { on_probe_ack_ = std::move(cb); }
    void setProbeCancelAckCallback(ProbeCancelAckCallback cb) { on_probe_cancel_ack_ = std::move(cb); }
    void setProbeRejectCallback(ProbeRejectCallback cb) { on_probe_reject_ = std::move(cb); }
    
    // Probe stats
    uint64_t probes_sent() const noexcept { return probes_sent_; }
    uint64_t probes_acked() const noexcept { return probes_acked_; }
    uint64_t probes_cancelled() const noexcept { return probes_cancelled_; }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RESPONSE HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    
    void handle_response(const char* data, size_t len) noexcept {
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.22: CRITICAL - Log full response (this was missing before!)
        // ═══════════════════════════════════════════════════════════════════
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ [WS-RX] RECEIVED BINANCE RESPONSE (%zu bytes)                         \n", len);
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ %.*s\n", static_cast<int>(std::min(len, size_t(500))), data);
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
        
        OrderResponse resp;
        if (!parse_response(data, len, resp)) {
            printf("[BINANCE_RESP] ✗ Failed to parse response\n");
            return;
        }
        
        // v4.9.22: Log parsed fields
        printf("[BINANCE_RESP] Parsed: request_id=%llu http_status=%d success=%s order_id=%llu symbol_id=%u status=%s\n",
               static_cast<unsigned long long>(resp.request_id),
               resp.http_status,
               resp.success ? "true" : "false",
               static_cast<unsigned long long>(resp.order_id),
               resp.symbol_id,
               resp.status);
        
        if (resp.success) {
            printf("[BINANCE_RESP] ✓ SUCCESS http=%d order_status=%s\n", resp.http_status, resp.status);
            
            if (strcmp(resp.status, "FILLED") == 0 || 
                strcmp(resp.status, "PARTIALLY_FILLED") == 0) {
                ++orders_filled_;
                
                // Determine fill type and record latency
                FillType fill_type = FillType::TAKER;
                uint64_t send_ts_ns = 0;
                Side fill_side = Side::Buy;  // Default
                
                // Check if this was a maker order
                auto maker_it = pending_maker_orders_.find(resp.symbol_id);
                if (maker_it != pending_maker_orders_.end()) {
                    fill_type = FillType::MAKER;
                    send_ts_ns = maker_it->second.send_ts_ns;
                    fill_side = maker_it->second.side;
                    maker_it->second.filled = true;
                    maker_it->second.filled_price = resp.executed_price;
                    maker_it->second.fill_type = FillType::MAKER;
                    maker_it->second.order_id = resp.order_id;
                    ++maker_fills_;
                    printf("[BINANCE_FILL] MAKER fill! fee savings applied\n");
                    pending_maker_orders_.erase(maker_it);
                } else {
                    auto taker_it = pending_orders_.find(resp.symbol_id);
                    if (taker_it != pending_orders_.end()) {
                        fill_type = FillType::TAKER;
                        send_ts_ns = taker_it->second.send_ts_ns;
                        fill_side = taker_it->second.side;
                        ++taker_fills_;
                        pending_orders_.erase(taker_it);
                    }
                }
                
                // ═══════════════════════════════════════════════════════════════
                // v4.9.10: RECORD HOT-PATH LATENCY (send → FILL)
                // This is the ONLY latency that matters for trading
                // ═══════════════════════════════════════════════════════════════
                if (send_ts_ns > 0) {
                    uint64_t recv_ts_ns = get_timestamp_ns();
                    uint64_t latency_ns = recv_ts_ns - send_ts_ns;
                    latency_tracker_.record_ns(latency_ns);
                    
                    double latency_ms = static_cast<double>(latency_ns) / 1'000'000.0;
                    printf("[LATENCY] FILL latency: %.3fms (min=%.3f p10=%.3f p50=%.3f)\n",
                           latency_ms, latency_tracker_.min_ms(), 
                           latency_tracker_.p10_ms(), latency_tracker_.p50_ms());
                }
                
                printf("[BINANCE_FILL] Order filled! type=%s total=%llu\n",
                       fill_type == FillType::MAKER ? "MAKER" : "TAKER",
                       static_cast<unsigned long long>(orders_filled_));
                
                if (on_fill_) {
                    on_fill_(resp.symbol_id, fill_side, resp.executed_qty, resp.executed_price, fill_type);
                }
            } else if (strcmp(resp.status, "NEW") == 0 || 
                       (resp.http_status == 200 && resp.order_id > 0 && 
                        strcmp(resp.status, "CANCELED") != 0 && strcmp(resp.status, "EXPIRED") != 0)) {
                // ═══════════════════════════════════════════════════════════════
                // v4.9.32: Check if this is a PROBE order ACK
                // FIXED: ACK responses don't have "status":"NEW" field!
                // With newOrderRespType=ACK, we just get {orderId, symbol, ...}
                // So also match on http_status=200 + valid order_id
                // ═══════════════════════════════════════════════════════════════
                bool is_probe = false;
                
                // v4.9.22: Direct lookup by request_id (O(1) instead of O(n))
                auto probe_it = pending_probes_.find(resp.request_id);
                if (probe_it != pending_probes_.end() && !probe_it->second.acked) {
                    ProbeOrder& probe = probe_it->second;
                    probe.exchange_order_id = resp.order_id;
                    probe.ack_ts_ns = get_timestamp_ns();
                    probe.acked = true;
                    is_probe = true;
                    ++probes_acked_;
                    
                    // Record ACK latency
                    uint64_t ack_latency_ns = probe.ack_ts_ns - probe.send_ts_ns;
                    uint64_t ack_latency_us = ack_latency_ns / 1000;
                    latency_tracker_.record_ns(ack_latency_ns);
                    
                    // v4.9.23: Record in ExecutionQuality telemetry
                    const char* sym_name = get_symbol_name(probe.symbol_id);
                    if (sym_name) {
                        ExecutionQuality::instance().record_ack(sym_name, ack_latency_us);
                        ExecutionGovernor::instance().on_ack_success(sym_name);
                        ExecutionGovernor::instance().on_high_latency(sym_name, ack_latency_us);
                    }
                    
                    double ack_latency_ms = static_cast<double>(ack_latency_ns) / 1'000'000.0;
                    printf("\n╔═══════════════════════════════════════════════════════════════════╗\n");
                    printf("║ [PROBE_ACK] ✓ RESPONSE MATCHED!                                   ║\n");
                    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
                    printf("║   Request ID: %llu                                                \n",
                           static_cast<unsigned long long>(resp.request_id));
                    printf("║   Exchange Order ID: %llu                                         \n",
                           static_cast<unsigned long long>(resp.order_id));
                    printf("║   Latency: %.3fms                                                 \n", ack_latency_ms);
                    printf("║   Total samples: %zu                                              \n", latency_tracker_.count());
                    printf("║   Venue State: %s                                                 \n", 
                           venueStateToString(ExecutionGovernor::instance().state()));
                    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
                    
                    // Notify bootstrapper
                    if (on_probe_ack_) {
                        on_probe_ack_(probe.symbol_id, probe.client_order_id, resp.order_id, ack_latency_ns);
                    }
                }
                
                if (!is_probe) {
                    // Regular limit order placed, waiting for fill
                    auto it = pending_maker_orders_.find(resp.symbol_id);
                    if (it != pending_maker_orders_.end()) {
                        it->second.order_id = resp.order_id;
                        
                        // ═══════════════════════════════════════════════════════════════
                        // v4.9.10: RECORD HOT-PATH LATENCY (send → NEW ACK)
                        // ═══════════════════════════════════════════════════════════════
                        if (it->second.send_ts_ns > 0) {
                            uint64_t recv_ts_ns = get_timestamp_ns();
                            uint64_t latency_ns = recv_ts_ns - it->second.send_ts_ns;
                            latency_tracker_.record_ns(latency_ns);
                            
                            double latency_ms = static_cast<double>(latency_ns) / 1'000'000.0;
                            printf("[LATENCY] NEW ACK latency: %.3fms\n", latency_ms);
                        }
                        
                        printf("[BINANCE_NEW] LIMIT order placed, order_id=%llu, waiting for fill or timeout\n",
                               static_cast<unsigned long long>(resp.order_id));
                    }
                }
            } else if (strcmp(resp.status, "CANCELED") == 0) {
                // ═══════════════════════════════════════════════════════════════
                // v4.9.10: Handle CANCELED status (for probes and regular orders)
                // ═══════════════════════════════════════════════════════════════
                bool is_probe_cancel = false;
                for (auto it = pending_probes_.begin(); it != pending_probes_.end(); ) {
                    if (it->second.exchange_order_id == resp.order_id && it->second.acked && !it->second.cancelled) {
                        it->second.cancel_ack_ts_ns = get_timestamp_ns();
                        it->second.cancelled = true;
                        is_probe_cancel = true;
                        ++probes_cancelled_;
                        
                        // Record cancel latency if cancel was sent
                        if (it->second.cancel_send_ts_ns > 0) {
                            uint64_t cancel_latency_ns = it->second.cancel_ack_ts_ns - it->second.cancel_send_ts_ns;
                            double cancel_latency_ms = static_cast<double>(cancel_latency_ns) / 1'000'000.0;
                            printf("[PROBE_CANCEL_ACK] Cancel latency: %.3fms\n", cancel_latency_ms);
                        }
                        
                        // Notify bootstrapper
                        if (on_probe_cancel_ack_) {
                            on_probe_cancel_ack_(resp.order_id);
                        }
                        
                        // Remove completed probe
                        it = pending_probes_.erase(it);
                        break;
                    } else {
                        ++it;
                    }
                }
                
                if (!is_probe_cancel) {
                    // Regular order canceled (e.g., maker timeout)
                    printf("[BINANCE_CANCELED] Order %llu canceled\n", 
                           static_cast<unsigned long long>(resp.order_id));
                    pending_maker_orders_.erase(resp.symbol_id);
                }
            }
        } else {
            ++orders_rejected_;
            
            // v4.9.23: Extract error code for governor
            int error_code = 0;
            if (resp.error_msg[0] == '[') {
                sscanf(resp.error_msg, "[%d]", &error_code);
            }
            
            // v4.9.27: Track signature rejections specifically
            if (error_code == -1022) {
                Binance::SignatureRejectionTracker::instance().record_rejection();
            }
            
            // v4.9.22: Enhanced error logging
            printf("\n╔═══════════════════════════════════════════════════════════════════╗\n");
            printf("║ [BINANCE_REJECT] ✗ ORDER REJECTED                                 ║\n");
            printf("╠═══════════════════════════════════════════════════════════════════╣\n");
            printf("║   Request ID: %llu                                                \n",
                   static_cast<unsigned long long>(resp.request_id));
            printf("║   HTTP Status: %d                                                 \n", resp.http_status);
            printf("║   Error Code: %d                                                  \n", error_code);
            printf("║   Error: %s\n", resp.error_msg);
            printf("║   Total Rejects: %llu                                             \n",
                   static_cast<unsigned long long>(orders_rejected_));
            printf("╚═══════════════════════════════════════════════════════════════════╝\n");
            
            // v4.9.22: Check if this is a probe rejection by request_id
            auto probe_it = pending_probes_.find(resp.request_id);
            if (probe_it != pending_probes_.end()) {
                const char* sym_name = get_symbol_name(probe_it->second.symbol_id);
                
                printf("[PROBE_REJECT] Probe request_id=%llu rejected\n",
                       static_cast<unsigned long long>(resp.request_id));
                
                // v4.9.23: Record in ExecutionQuality telemetry
                if (sym_name) {
                    ExecutionQuality::instance().record_reject(sym_name, resp.error_msg);
                    ExecutionGovernor::instance().on_order_error(sym_name, error_code);
                }
                
                if (on_probe_reject_) {
                    on_probe_reject_(probe_it->second.client_order_id);
                }
                pending_probes_.erase(probe_it);
            } else {
                // Regular order rejection
                const char* sym_name = get_symbol_name(resp.symbol_id);
                if (sym_name) {
                    ExecutionQuality::instance().record_reject(sym_name, resp.error_msg);
                    ExecutionGovernor::instance().on_order_error(sym_name, error_code);
                }
            }
            
            // Remove from pending regular orders
            pending_maker_orders_.erase(resp.symbol_id);
            pending_orders_.erase(resp.symbol_id);
            
            if (on_reject_) {
                on_reject_(resp.symbol_id, resp.error_msg);
            }
        }
    }
    
    [[nodiscard]] bool parse_response(const char* json, size_t len, OrderResponse& out) noexcept {
        out.request_id = 0;
        out.http_status = 0;
        out.order_id = 0;
        out.client_order_id = 0;
        out.symbol_id = 0;
        out.executed_qty = 0.0;
        out.executed_price = 0.0;
        out.success = false;
        out.status[0] = '\0';
        out.error_msg[0] = '\0';
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.22: Parse outer "id" field FIRST (for request matching)
        // Format: {"id":"123",...} or {"id":123,...}
        // ═══════════════════════════════════════════════════════════════════
        const char* id_field = strstr(json, "\"id\"");
        if (id_field) {
            id_field = strchr(id_field, ':');
            if (id_field) {
                ++id_field;
                // Skip whitespace
                while (*id_field == ' ') ++id_field;
                // Check if quoted string or number
                if (*id_field == '"') {
                    out.request_id = FastParse::to_uint64(id_field + 1, 20);
                } else {
                    out.request_id = FastParse::to_uint64(id_field, 20);
                }
            }
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.22: Parse outer "status" (HTTP-like: 200, 400, etc)
        // This is DIFFERENT from result.status ("NEW", "FILLED", etc)
        // ═══════════════════════════════════════════════════════════════════
        const char* http_status = strstr(json, "\"status\"");
        if (http_status) {
            http_status = strchr(http_status, ':');
            if (http_status) {
                out.http_status = static_cast<int>(FastParse::to_uint64(http_status + 1, 10));
            }
        }
        
        const char* error = strstr(json, "\"error\"");
        if (error) {
            out.success = false;
            const char* msg = strstr(json, "\"msg\"");
            if (msg) {
                msg = strchr(msg, ':');
                if (msg) {
                    msg = strchr(msg, '"');
                    if (msg) {
                        ++msg;
                        const char* end = strchr(msg, '"');
                        if (end) {
                            size_t copy_len = end - msg;
                            if (copy_len > 127) copy_len = 127;
                            memcpy(out.error_msg, msg, copy_len);
                            out.error_msg[copy_len] = '\0';
                        }
                    }
                }
            }
            // Also extract error code for diagnostic
            const char* code = strstr(json, "\"code\"");
            if (code) {
                code = strchr(code, ':');
                if (code) {
                    ++code;
                    // Skip whitespace
                    while (*code == ' ') ++code;
                    // Handle negative codes like -1022
                    bool negative = (*code == '-');
                    if (negative) ++code;
                    int err_code = static_cast<int>(FastParse::to_uint64(code, 10));
                    if (negative) err_code = -err_code;
                    // Prepend error code to message if there's room
                    char tmp[128];
                    snprintf(tmp, sizeof(tmp), "[%d] %s", err_code, out.error_msg);
                    strncpy(out.error_msg, tmp, sizeof(out.error_msg) - 1);
                    out.error_msg[sizeof(out.error_msg) - 1] = '\0';
                }
            }
            return true;
        }
        
        const char* result = strstr(json, "\"result\"");
        if (!result) {
            return false;
        }
        
        out.success = true;
        
        const char* order_id = strstr(json, "\"orderId\"");
        if (order_id) {
            order_id = strchr(order_id, ':');
            if (order_id) {
                out.order_id = FastParse::to_uint64(order_id + 1, len - (order_id - json));
            }
        }
        
        const char* symbol = strstr(json, "\"symbol\"");
        if (symbol) {
            symbol = strchr(symbol, ':');
            if (symbol) {
                symbol = strchr(symbol, '"');
                if (symbol) {
                    ++symbol;
                    const char* end = strchr(symbol, '"');
                    if (end) {
                        out.symbol_id = symbol_to_id(symbol, end - symbol);
                    }
                }
            }
        }
        
        const char* status = strstr(json, "\"status\"");
        if (status) {
            status = strchr(status, ':');
            if (status) {
                status = strchr(status, '"');
                if (status) {
                    ++status;
                    const char* end = strchr(status, '"');
                    if (end) {
                        size_t copy_len = end - status;
                        if (copy_len > 15) copy_len = 15;
                        memcpy(out.status, status, copy_len);
                        out.status[copy_len] = '\0';
                    }
                }
            }
        }
        
        const char* exec_qty = strstr(json, "\"executedQty\"");
        if (exec_qty) {
            exec_qty = strchr(exec_qty, ':');
            if (exec_qty) {
                exec_qty = strchr(exec_qty, '"');
                if (exec_qty) {
                    out.executed_qty = FastParse::to_double(exec_qty + 1, 20);
                }
            }
        }
        
        const char* price = strstr(json, "\"avgPrice\"");
        if (!price) price = strstr(json, "\"price\"");
        if (price) {
            price = strchr(price, ':');
            if (price) {
                price = strchr(price, '"');
                if (price) {
                    out.executed_price = FastParse::to_double(price + 1, 20);
                }
            }
        }
        
        return true;
    }
    
    static uint64_t get_timestamp_ns() noexcept {
        return Chimera::now_ns_monotonic();  // v4.9.10: Use consistent monotonic clock
    }
    
    static uint64_t get_timestamp_ms() noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER DATA
    // ═══════════════════════════════════════════════════════════════════════
    
    OrderQueue<256>& order_queue_;
    GlobalKill& global_kill_;
    const Config& config_;
    WebSocketConnection ws_;
    HMACSigner signer_;
    std::thread thread_;
    std::atomic<bool> running_;
    
    // Pending orders
    std::unordered_map<uint16_t, PendingOrder> pending_orders_;
    std::unordered_map<uint16_t, PendingOrder> pending_maker_orders_;
    
    // v4.9.10: Pending probe orders (keyed by client_order_id)
    std::unordered_map<uint64_t, ProbeOrder> pending_probes_;
    
    // Stats
    uint64_t orders_sent_;
    uint64_t orders_filled_;
    uint64_t orders_rejected_;
    uint64_t blocked_orders_;
    uint64_t paper_fills_;
    uint64_t maker_fills_;
    uint64_t taker_fills_;
    uint64_t maker_timeouts_;
    
    // v4.9.10: Probe stats
    uint64_t probes_sent_ = 0;
    uint64_t probes_acked_ = 0;
    uint64_t probes_cancelled_ = 0;
    
    std::atomic<bool> intent_is_live_;
    uint32_t maker_timeout_ms_;
    
    // v4.9.10: Hot-path latency tracker
    Chimera::HotPathLatencyTracker latency_tracker_;
    
    // v4.9.12: Probe send failure throttling
    uint64_t last_probe_fail_log_ns_ = 0;            // Last time we logged a probe failure
    uint64_t suppressed_probe_fails_ = 0;            // Count of suppressed log messages
    static constexpr uint64_t PROBE_FAIL_THROTTLE_NS = 30'000'000'000ULL;  // 30 seconds
    
    // Callbacks
    OnFill on_fill_;
    OnReject on_reject_;
    OnMakerTimeout on_maker_timeout_;
    
    // v4.9.10: Probe callbacks for bootstrapper
    ProbeAckCallback on_probe_ack_;
    ProbeCancelAckCallback on_probe_cancel_ack_;
    ProbeRejectCallback on_probe_reject_;
};

} // namespace Binance
} // namespace Chimera
