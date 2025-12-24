// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/BinanceOrderSender.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// PURPOSE: WebSocket API order execution for Binance
// OWNER: Jo
// LAST VERIFIED: 2024-12-21
//
// DESIGN:
// - Dedicated thread for order sending
// - Consumes from lock-free order queue
// - Uses WebSocket API (NOT REST) for minimum latency
// - HMAC-SHA256 signed requests
// - Handles order responses and updates ExecutionGate
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <functional>

#include "BinanceConfig.hpp"
#include "BinanceWebSocket.hpp"
#include "BinanceHMAC.hpp"
#include "BinanceParser.hpp"
#include "SymbolThread.hpp"

namespace Chimera {
namespace Binance {

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
    char     status[16];      // NEW, FILLED, CANCELED, REJECTED, etc.
    char     error_msg[128];
};

// ─────────────────────────────────────────────────────────────────────────────
// Order Sender Thread
// ─────────────────────────────────────────────────────────────────────────────
class OrderSender {
public:
    using OnFill = std::function<void(uint16_t symbol_id, Side side, double qty, double price)>;
    using OnReject = std::function<void(uint16_t symbol_id, const char* reason)>;
    
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
    {}
    
    ~OrderSender() {
        stop();
    }
    
    // Set callbacks
    void set_on_fill(OnFill cb) noexcept { on_fill_ = cb; }
    void set_on_reject(OnReject cb) noexcept { on_reject_ = cb; }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool start() noexcept {
        if (running_.load()) return true;
        
        // v6.95: Check if API keys are configured (required for trading)
        if (config_.api_key == nullptr || config_.secret_key == nullptr) {
            std::cerr << "[OrderSender] No API keys configured - trading disabled (market data only mode)\n";
            return false;
        }
        
        // Connect to WebSocket API
        if (!ws_.connect(config_.ws_api_host, config_.ws_api_port, config_.ws_api_path)) {
            return false;
        }
        
        running_.store(true);
        thread_ = std::thread(&OrderSender::run, this);
        return true;
    }
    
    void stop() noexcept {
        running_.store(false);
        if (thread_.joinable()) {
            thread_.join();
        }
        ws_.disconnect();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] uint64_t orders_sent() const noexcept { return orders_sent_; }
    [[nodiscard]] uint64_t orders_filled() const noexcept { return orders_filled_; }
    [[nodiscard]] uint64_t orders_rejected() const noexcept { return orders_rejected_; }
    [[nodiscard]] bool is_connected() const noexcept { return ws_.is_connected(); }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // MAIN LOOP
    // ═══════════════════════════════════════════════════════════════════════
    
    void run() noexcept {
        while (running_.load(std::memory_order_relaxed)) {
            // Check global kill
            if (global_kill_.killed()) {
                break;
            }
            
            // Check WebSocket connection
            if (!ws_.is_connected()) {
                // Try to reconnect
                if (!ws_.reconnect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
            
            // Process incoming responses (ignore return - best effort)
            (void)ws_.poll([this](const char* data, size_t len, WSOpcode opcode) {
                if (opcode == WSOpcode::TEXT) {
                    handle_response(data, len);
                }
            });
            
            // Process outgoing orders
            OrderIntent intent;
            if (order_queue_.pop(intent)) {
                send_order(intent);
            } else {
                // No orders to send - brief sleep
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ORDER SENDING
    // v6.88 FIX: Normalize quantity at execution layer (THE FINAL FIX)
    // ═══════════════════════════════════════════════════════════════════════
    
    // Binance LOT_SIZE filter compliance
    [[nodiscard]] static double normalize_qty(double qty, double minQty, double stepSize) noexcept {
        if (qty < minQty) qty = minQty;
        double steps = std::floor(qty / stepSize);
        double norm = steps * stepSize;
        if (norm < minQty) norm = minQty;
        return norm;
    }
    
    void send_order(const OrderIntent& intent) noexcept {
        // Find symbol config
        const SymbolConfig* sym = find_symbol(intent.symbol_id);
        if (!sym) return;
        
        // v6.88 FIX: Normalize quantity HERE at execution layer
        // This is THE place where it must happen - right before send
        double final_qty = normalize_qty(intent.quantity, sym->lot_size, sym->lot_size);
        
        // v6.88: Debug log - MUST see normalized quantity
        std::cout << "\n[BTC_EXEC_QTY] " << sym->symbol 
                  << " raw=" << intent.quantity 
                  << " norm=" << final_qty 
                  << " lot=" << sym->lot_size << "\n";
        
        // Build order request
        const char* side = (intent.side == Side::Buy) ? "BUY" : "SELL";
        const char* type = "MARKET";  // For now, just market orders
        
        char client_order_id[32];
        snprintf(client_order_id, sizeof(client_order_id), "CHM%llu", static_cast<unsigned long long>(get_timestamp_ms()));
        
        WSAPIRequestBuilder builder;
        const char* request = builder.build_new_order(
            sym->symbol,
            side,
            type,
            final_qty,          // v6.88: USE NORMALIZED QUANTITY
            sym->qty_precision,
            0.0,  // price (ignored for market)
            sym->price_precision,
            "GTC",  // time_in_force (ignored for market)
            client_order_id,
            signer_,
            config_.api_key
        );
        
        std::cout << "[EXEC_SEND] " << sym->symbol << " " << side << " " << final_qty << "\n";
        
        // Send request
        if (ws_.send_text(request, builder.length())) {
            ++orders_sent_;
            std::cout << "[EXEC_SENT_OK] Order sent successfully, total=" << orders_sent_ << "\n";
            
            // Track pending order
            pending_orders_[intent.symbol_id] = intent;
        } else {
            std::cout << "[EXEC_SEND_FAIL] WebSocket send failed! connected=" << ws_.is_connected() << "\n";
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RESPONSE HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    
    void handle_response(const char* data, size_t len) noexcept {
        // v6.88: Log all responses for debugging
        std::cout << "[BINANCE_RESP] " << std::string(data, std::min(len, size_t(200))) << "\n";
        
        OrderResponse resp;
        
        if (!parse_response(data, len, resp)) {
            std::cout << "[BINANCE_RESP] Failed to parse response\n";
            return;
        }
        
        if (resp.success) {
            std::cout << "[BINANCE_RESP] SUCCESS status=" << resp.status << "\n";
            // Check if filled
            if (strcmp(resp.status, "FILLED") == 0 || 
                strcmp(resp.status, "PARTIALLY_FILLED") == 0) {
                ++orders_filled_;
                std::cout << "[BINANCE_FILL] Order filled! total=" << orders_filled_ << "\n";
                
                // Get original intent
                auto it = pending_orders_.find(resp.symbol_id);
                if (it != pending_orders_.end() && on_fill_) {
                    on_fill_(resp.symbol_id, it->second.side, 
                            resp.executed_qty, resp.executed_price);
                    pending_orders_.erase(it);
                }
            }
        } else {
            ++orders_rejected_;
            std::cout << "[BINANCE_REJECT] Order rejected! error=" << resp.error_msg 
                      << " total_rejects=" << orders_rejected_ << "\n";
            
            if (on_reject_) {
                on_reject_(resp.symbol_id, resp.error_msg);
            }
            
            // Remove from pending
            pending_orders_.erase(resp.symbol_id);
        }
    }
    
    [[nodiscard]] bool parse_response(const char* json, size_t len, OrderResponse& out) noexcept {
        // Initialize
        out.order_id = 0;
        out.client_order_id = 0;
        out.symbol_id = 0;
        out.executed_qty = 0.0;
        out.executed_price = 0.0;
        out.success = false;
        out.status[0] = '\0';
        out.error_msg[0] = '\0';
        
        // Check for error
        const char* error = strstr(json, "\"error\"");
        if (error) {
            out.success = false;
            
            // Extract error message
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
        
        // Check for result
        const char* result = strstr(json, "\"result\"");
        if (!result) {
            return false;
        }
        
        out.success = true;
        
        // Parse orderId
        const char* order_id = strstr(json, "\"orderId\"");
        if (order_id) {
            order_id = strchr(order_id, ':');
            if (order_id) {
                out.order_id = FastParse::to_uint64(order_id + 1, len - (order_id - json));
            }
        }
        
        // Parse symbol
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
        
        // Parse status
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
        
        // Parse executedQty
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
        
        // Parse avgPrice or price
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
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER DATA
    // ═══════════════════════════════════════════════════════════════════════
    
    // References
    OrderQueue<256>& order_queue_;
    GlobalKill& global_kill_;
    const Config& config_;
    
    // WebSocket connection
    WebSocketConnection ws_;
    
    // HMAC signer
    HMACSigner signer_;
    
    // Thread
    std::thread thread_;
    std::atomic<bool> running_;
    
    // Pending orders (symbol_id -> intent)
    std::unordered_map<uint16_t, OrderIntent> pending_orders_;
    
    // Stats
    uint64_t orders_sent_;
    uint64_t orders_filled_;
    uint64_t orders_rejected_;
    
    // Callbacks
    OnFill on_fill_;
    OnReject on_reject_;
};

} // namespace Binance
} // namespace Chimera
