// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceOrderSender.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: WebSocket API order execution for Binance
// OWNER: Jo
// LAST VERIFIED: 2025-01-01
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
// Order Response
// ─────────────────────────────────────────────────────────────────────────────
struct OrderResponse {
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
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool start() noexcept {
        if (running_.load()) return true;
        
        if (config_.api_key == nullptr || config_.secret_key == nullptr) {
            std::cerr << "[OrderSender] No API keys configured - trading disabled\n";
            return false;
        }
        
        if (!ws_.connect(config_.ws_api_host, config_.ws_api_port, config_.ws_api_path)) {
            return false;
        }
        
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
    // ═══════════════════════════════════════════════════════════════════════
    
    void run() noexcept {
        while (running_.load(std::memory_order_relaxed)) {
            if (global_kill_.killed()) {
                break;
            }
            
            if (!ws_.is_connected()) {
                if (!ws_.reconnect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
            
            // Poll for responses
            (void)ws_.poll([this](const char* data, size_t len, WSOpcode opcode) {
                if (opcode == WSOpcode::TEXT) {
                    handle_response(data, len);
                }
            });
            
            // Check for maker order timeouts
            check_maker_timeouts();
            
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
        
        if (ws_.send_text(request, builder.length())) {
            ++orders_sent_;
            
            // Track pending maker order
            PendingOrder pending;
            pending.symbol_id = symbol_id;
            pending.side = side;
            pending.quantity = qty;
            pending.limit_price = price;
            pending.send_ts_ns = get_timestamp_ns();
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
        
        if (ws_.send_text(request, builder.length())) {
            ++orders_sent_;
            
            // Track as taker order (no timeout needed)
            PendingOrder pending;
            pending.symbol_id = symbol_id;
            pending.side = side;
            pending.quantity = qty;
            pending.limit_price = 0.0;
            pending.send_ts_ns = get_timestamp_ns();
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
    
    // ═══════════════════════════════════════════════════════════════════════
    // RESPONSE HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    
    void handle_response(const char* data, size_t len) noexcept {
        std::cout << "[BINANCE_RESP] " << std::string(data, std::min(len, size_t(200))) << "\n";
        
        OrderResponse resp;
        if (!parse_response(data, len, resp)) {
            std::cout << "[BINANCE_RESP] Failed to parse response\n";
            return;
        }
        
        if (resp.success) {
            std::cout << "[BINANCE_RESP] SUCCESS status=" << resp.status << "\n";
            
            if (strcmp(resp.status, "FILLED") == 0 || 
                strcmp(resp.status, "PARTIALLY_FILLED") == 0) {
                ++orders_filled_;
                
                // Determine fill type
                FillType fill_type = FillType::TAKER;
                
                // Check if this was a maker order
                auto maker_it = pending_maker_orders_.find(resp.symbol_id);
                if (maker_it != pending_maker_orders_.end()) {
                    fill_type = FillType::MAKER;
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
                        ++taker_fills_;
                        pending_orders_.erase(taker_it);
                    }
                }
                
                printf("[BINANCE_FILL] Order filled! type=%s total=%llu\n",
                       fill_type == FillType::MAKER ? "MAKER" : "TAKER",
                       static_cast<unsigned long long>(orders_filled_));
                
                if (on_fill_) {
                    // Need to find the side from pending order
                    Side fill_side = Side::Buy;  // Default
                    auto it = pending_orders_.find(resp.symbol_id);
                    if (it != pending_orders_.end()) {
                        fill_side = it->second.side;
                    }
                    
                    on_fill_(resp.symbol_id, fill_side, resp.executed_qty, resp.executed_price, fill_type);
                }
            } else if (strcmp(resp.status, "NEW") == 0) {
                // Limit order placed, waiting for fill
                auto it = pending_maker_orders_.find(resp.symbol_id);
                if (it != pending_maker_orders_.end()) {
                    it->second.order_id = resp.order_id;
                    printf("[BINANCE_NEW] LIMIT order placed, order_id=%llu, waiting for fill or timeout\n",
                           static_cast<unsigned long long>(resp.order_id));
                }
            }
        } else {
            ++orders_rejected_;
            std::cout << "[BINANCE_REJECT] Order rejected! error=" << resp.error_msg 
                      << " total_rejects=" << orders_rejected_ << "\n";
            
            // Remove from pending
            pending_maker_orders_.erase(resp.symbol_id);
            pending_orders_.erase(resp.symbol_id);
            
            if (on_reject_) {
                on_reject_(resp.symbol_id, resp.error_msg);
            }
        }
    }
    
    [[nodiscard]] bool parse_response(const char* json, size_t len, OrderResponse& out) noexcept {
        out.order_id = 0;
        out.client_order_id = 0;
        out.symbol_id = 0;
        out.executed_qty = 0.0;
        out.executed_price = 0.0;
        out.success = false;
        out.status[0] = '\0';
        out.error_msg[0] = '\0';
        
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
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
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
    
    // Stats
    uint64_t orders_sent_;
    uint64_t orders_filled_;
    uint64_t orders_rejected_;
    uint64_t blocked_orders_;
    uint64_t paper_fills_;
    uint64_t maker_fills_;
    uint64_t taker_fills_;
    uint64_t maker_timeouts_;
    
    std::atomic<bool> intent_is_live_;
    uint32_t maker_timeout_ms_;
    
    // Callbacks
    OnFill on_fill_;
    OnReject on_reject_;
    OnMakerTimeout on_maker_timeout_;
};

} // namespace Binance
} // namespace Chimera
