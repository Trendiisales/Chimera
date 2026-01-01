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
// v4.5.1: Added GlobalRiskGovernor check as final defense layer
// v4.7.0: Added ExecutionAuthority as THE FIRST GATE
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

// v4.5.1: Final execution defense
#include "shared/GlobalRiskGovernor.hpp"
#include "core/EngineOwnership.hpp"

// v4.7.0: ExecutionAuthority - THE single choke point
#include "core/ExecutionAuthority.hpp"

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
        , blocked_orders_(0)
        , paper_fills_(0)
        , intent_is_live_(false)
    {}
    
    ~OrderSender() {
        stop();
    }
    
    // Set callbacks
    void set_on_fill(OnFill cb) noexcept { on_fill_ = cb; }
    void set_on_reject(OnReject cb) noexcept { on_reject_ = cb; }
    
    // v4.7.0: Set intent state from main loop
    void setIntentLive(bool live) noexcept { intent_is_live_.store(live, std::memory_order_release); }
    bool isIntentLive() const noexcept { return intent_is_live_.load(std::memory_order_acquire); }
    
    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool start() noexcept {
        if (running_.load()) return true;
        
        if (config_.api_key == nullptr || config_.secret_key == nullptr) {
            std::cerr << "[OrderSender] No API keys configured - trading disabled (market data only mode)\n";
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
            
            (void)ws_.poll([this](const char* data, size_t len, WSOpcode opcode) {
                if (opcode == WSOpcode::TEXT) {
                    handle_response(data, len);
                }
            });
            
            OrderIntent intent;
            if (order_queue_.pop(intent)) {
                send_order(intent);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
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
        // ═══════════════════════════════════════════════════════════════════
        // v4.7.0: EXECUTION AUTHORITY - THE SINGLE CHOKE POINT
        // THIS IS THE FIRST AND MOST IMPORTANT GATE.
        // If intent != LIVE, NOTHING passes. NO EXCEPTIONS.
        // ═══════════════════════════════════════════════════════════════════
        const SymbolConfig* sym = find_symbol(intent.symbol_id);
        const char* sym_name = sym ? sym->symbol : "UNKNOWN";
        
        Chimera::ExecBlockReason block_reason;
        bool intent_live = intent_is_live_.load(std::memory_order_acquire);
        if (!Chimera::getExecutionAuthority().allowCrypto(sym_name, intent_live, &block_reason)) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  ⛔ ORDER BLOCKED - EXECUTION AUTHORITY                   ║\n";
            std::cout << "║  Symbol: " << sym_name << "\n";
            std::cout << "║  Reason: " << Chimera::execBlockReasonToString(block_reason) << "\n";
            std::cout << "║  Intent Live: " << (intent_live ? "YES" : "NO") << "\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++blocked_orders_;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.5.1: FINAL DEFENSE - GLOBAL RISK GOVERNOR
        // ═══════════════════════════════════════════════════════════════════
        if (!Chimera::GlobalRiskGovernor::instance().canSubmitOrder(Chimera::EngineId::BINANCE)) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  ⛔ ORDER BLOCKED - RISK GOVERNOR                        ║\n";
            std::cout << "║  Daily loss limit or throttle active                     ║\n";
            std::cout << "║  NO orders will be sent to Binance                       ║\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++blocked_orders_;
            return;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v3.0: THREE-LAYER TRADE SAFETY
        // ═══════════════════════════════════════════════════════════════════
        
        const char* side = (intent.side == Side::Buy) ? "BUY" : "SELL";
        
        if (!is_trading_logic_enabled()) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  🔒 ORDER BLOCKED - SHADOW MODE                           ║\n";
            std::cout << "║  " << sym_name << " " << side << " qty=" << intent.quantity << "\n";
            std::cout << "║  trade_mode=" << trade_mode_str() << "                                        ║\n";
            std::cout << "║  Signal recorded, NO order sent                           ║\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++blocked_orders_;
            return;
        }
        
        if (is_paper_mode()) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  📄 PAPER MODE - SIMULATED FILL                          ║\n";
            std::cout << "║  " << sym_name << " " << side << " qty=" << intent.quantity << "\n";
            std::cout << "║  Routed to ShadowExecutor                                 ║\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
            ++paper_fills_;
            if (on_fill_) {
                on_fill_(intent.symbol_id, intent.side, intent.quantity, intent.price);
            }
            return;
        }
        
        if (!is_live_trading_enabled()) {
            std::cout << "[EXEC] ERROR: Reached live execution but is_live_trading_enabled() is false!\n";
            return;
        }
        
        if (!sym) return;
        
        double final_qty = normalize_qty(intent.quantity, sym->lot_size, sym->lot_size);
        
        std::cout << "\n[BTC_EXEC_QTY] " << sym->symbol 
                  << " raw=" << intent.quantity 
                  << " norm=" << final_qty 
                  << " lot=" << sym->lot_size << "\n";
        
        const char* type = "MARKET";
        
        char client_order_id[32];
        snprintf(client_order_id, sizeof(client_order_id), "CHM%llu", static_cast<unsigned long long>(get_timestamp_ms()));
        
        WSAPIRequestBuilder builder;
        const char* request = builder.build_new_order(
            sym->symbol,
            side,
            type,
            final_qty,
            sym->qty_precision,
            0.0,
            sym->price_precision,
            "GTC",
            client_order_id,
            signer_,
            config_.api_key
        );
        
        std::cout << "[EXEC_SEND] " << sym->symbol << " " << side << " " << final_qty << "\n";
        
        if (ws_.send_text(request, builder.length())) {
            ++orders_sent_;
            std::cout << "[EXEC_SENT_OK] Order sent successfully, total=" << orders_sent_ << "\n";
            pending_orders_[intent.symbol_id] = intent;
        } else {
            std::cout << "[EXEC_SEND_FAIL] WebSocket send failed! connected=" << ws_.is_connected() << "\n";
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
                std::cout << "[BINANCE_FILL] Order filled! total=" << orders_filled_ << "\n";
                
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
            pending_orders_.erase(resp.symbol_id);
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
    std::unordered_map<uint16_t, OrderIntent> pending_orders_;
    uint64_t orders_sent_;
    uint64_t orders_filled_;
    uint64_t orders_rejected_;
    uint64_t blocked_orders_;
    uint64_t paper_fills_;
    std::atomic<bool> intent_is_live_;
    OnFill on_fill_;
    OnReject on_reject_;
};

} // namespace Binance
} // namespace Chimera
