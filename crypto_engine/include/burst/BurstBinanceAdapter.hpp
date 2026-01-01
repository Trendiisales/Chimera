// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/burst/BurstBinanceAdapter.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE
// VERSION: v1.0.0
// OWNER: Jo
//
// Adapter to wire CryptoBurstEngine to Binance WebSocket feeds and order sender.
// This is the glue code - it converts Binance formats to Burst engine formats.
//
// USAGE:
//   1. Create BurstBinanceAdapter with engine and order sender
//   2. Call on_depth_update() from Binance depth stream callback
//   3. Call on_agg_trade() from Binance aggTrade stream callback
//   4. Engine will call order sender when signals fire
//
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "CryptoBurstEngine.hpp"
#include "../binance/BinanceOrderSender.hpp"
#include "../binance/OrderBook.hpp"

#include <memory>
#include <string>
#include <iostream>
#include <iomanip>

namespace chimera {
namespace crypto {
namespace burst {

// ═══════════════════════════════════════════════════════════════════════════════
// BINANCE DATA CONVERTERS
// ═══════════════════════════════════════════════════════════════════════════════

inline BurstSymbol parse_symbol(const std::string& sym) {
    if (sym == "BTCUSDT" || sym == "btcusdt") return BurstSymbol::BTCUSDT;
    if (sym == "ETHUSDT" || sym == "ethusdt") return BurstSymbol::ETHUSDT;
    if (sym == "SOLUSDT" || sym == "solusdt") return BurstSymbol::SOLUSDT;
    return BurstSymbol::BTCUSDT; // Default
}

inline std::string symbol_to_binance(BurstSymbol sym) {
    switch (sym) {
        case BurstSymbol::BTCUSDT: return "BTCUSDT";
        case BurstSymbol::ETHUSDT: return "ETHUSDT";
        case BurstSymbol::SOLUSDT: return "SOLUSDT";
        default: return "BTCUSDT";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// BURST BINANCE ADAPTER
// ═══════════════════════════════════════════════════════════════════════════════

class BurstBinanceAdapter {
public:
    /**
     * Construct adapter with engine and optional order sender
     * @param engine The burst engine to feed data to
     * @param order_sender Binance order sender (nullptr for shadow mode)
     */
    BurstBinanceAdapter(
        std::shared_ptr<CryptoBurstEngine> engine,
        std::shared_ptr<BinanceOrderSender> order_sender = nullptr
    ) : engine_(std::move(engine))
      , order_sender_(std::move(order_sender))
      , pending_entry_symbol_(BurstSymbol::BTCUSDT)
      , pending_entry_dir_(Direction::NONE)
      , pending_entry_size_(0.0)
    {
        // Wire up engine callbacks
        engine_->set_on_entry_signal([this](const BurstEntrySignal& sig) {
            on_entry_signal(sig);
        });
        
        engine_->set_on_exit_signal([this](const BurstExitSignal& sig) {
            on_exit_signal(sig);
        });
        
        engine_->set_on_trade_result([this](const BurstTradeResult& result) {
            on_trade_result(result);
        });
        
        engine_->set_on_idle_log([this](BurstSymbol sym, const GateStatus& status) {
            on_idle_log(sym, status);
        });
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // DATA FEED HANDLERS (Call from WebSocket callbacks)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * Handle depth update from Binance WebSocket
     * Call this from your depth stream callback
     * 
     * @param symbol Symbol string (e.g., "BTCUSDT")
     * @param bids Vector of [price, qty] pairs, best first
     * @param asks Vector of [price, qty] pairs, best first
     * @param exchange_ts Exchange timestamp in milliseconds
     */
    void on_depth_update(
        const std::string& symbol,
        const std::vector<std::pair<double, double>>& bids,
        const std::vector<std::pair<double, double>>& asks,
        uint64_t exchange_ts
    ) {
        BurstBook book;
        book.symbol = parse_symbol(symbol);
        book.exchange_ts = exchange_ts;
        book.local_ts = now_us();
        
        // Copy bids
        book.bid_levels = std::min((size_t)20, bids.size());
        for (size_t i = 0; i < book.bid_levels; ++i) {
            book.bids[i].price = bids[i].first;
            book.bids[i].qty = bids[i].second;
        }
        
        // Copy asks
        book.ask_levels = std::min((size_t)20, asks.size());
        for (size_t i = 0; i < book.ask_levels; ++i) {
            book.asks[i].price = asks[i].first;
            book.asks[i].qty = asks[i].second;
        }
        
        engine_->on_book_update(book);
    }
    
    /**
     * Handle depth update from Binance OrderBook class
     */
    void on_depth_update(const Binance::OrderBook& ob, uint64_t exchange_ts) {
        BurstBook book;
        book.symbol = parse_symbol(ob.symbol());
        book.exchange_ts = exchange_ts;
        book.local_ts = now_us();
        
        // Get bids/asks from OrderBook
        auto bids = ob.bids(20);
        auto asks = ob.asks(20);
        
        book.bid_levels = std::min((size_t)20, bids.size());
        for (size_t i = 0; i < book.bid_levels; ++i) {
            book.bids[i].price = bids[i].price;
            book.bids[i].qty = bids[i].qty;
        }
        
        book.ask_levels = std::min((size_t)20, asks.size());
        for (size_t i = 0; i < book.ask_levels; ++i) {
            book.asks[i].price = asks[i].price;
            book.asks[i].qty = asks[i].qty;
        }
        
        engine_->on_book_update(book);
    }
    
    /**
     * Handle aggregate trade from Binance WebSocket
     * Call this from your aggTrade stream callback
     */
    void on_agg_trade(
        const std::string& symbol,
        double price,
        double qty,
        bool is_buyer_maker,
        uint64_t exchange_ts
    ) {
        BurstTrade trade;
        trade.symbol = parse_symbol(symbol);
        trade.price = price;
        trade.qty = qty;
        trade.is_buyer_maker = is_buyer_maker;
        trade.exchange_ts = exchange_ts;
        trade.local_ts = now_us();
        
        engine_->on_trade(trade);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ORDER FILL HANDLERS (Call after order execution)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * Notify adapter of entry order fill
     */
    void on_entry_fill(const std::string& symbol, double fill_price, double fill_size) {
        BurstSymbol sym = parse_symbol(symbol);
        
        // Use pending entry direction
        Direction dir = pending_entry_dir_;
        pending_entry_dir_ = Direction::NONE;
        
        engine_->on_entry_fill(sym, dir, fill_price, fill_size);
    }
    
    /**
     * Notify adapter of exit order fill
     */
    void on_exit_fill(const std::string& symbol, double fill_price, ExitReason reason) {
        engine_->on_exit_fill(parse_symbol(symbol), fill_price, reason);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ACCESSORS
    // ═══════════════════════════════════════════════════════════════════════
    
    CryptoBurstEngine* engine() { return engine_.get(); }
    const CryptoBurstEngine* engine() const { return engine_.get(); }
    
    bool has_order_sender() const { return order_sender_ != nullptr; }

private:
    // ═══════════════════════════════════════════════════════════════════════
    // ENGINE CALLBACKS
    // ═══════════════════════════════════════════════════════════════════════
    
    void on_entry_signal(const BurstEntrySignal& signal) {
        std::cout << "[BURST-ADAPTER] Entry signal: " << symbol_str(signal.symbol)
                  << " " << direction_str(signal.direction)
                  << " size=" << std::fixed << std::setprecision(6) << signal.suggested_size
                  << " @ " << std::setprecision(2) << signal.entry_price << "\n";
        
        // Store pending entry for fill callback
        pending_entry_symbol_ = signal.symbol;
        pending_entry_dir_ = signal.direction;
        pending_entry_size_ = signal.suggested_size;
        
        if (!order_sender_) {
            // Shadow mode - simulate fill
            std::cout << "[BURST-ADAPTER] Shadow mode - simulating fill\n";
            engine_->on_entry_fill(signal.symbol, signal.direction, 
                                   signal.entry_price, signal.suggested_size);
            return;
        }
        
        // Live mode - send order via WebSocket API
        std::string binance_symbol = symbol_to_binance(signal.symbol);
        std::string side = (signal.direction == Direction::LONG) ? "BUY" : "SELL";
        
        // Send market order (taker)
        order_sender_->send_market_order(
            binance_symbol,
            side,
            signal.suggested_size,
            [this, signal](bool success, double fill_price, double fill_size, const std::string& error) {
                if (success) {
                    std::cout << "[BURST-ADAPTER] Entry filled @ " << fill_price << "\n";
                    engine_->on_entry_fill(signal.symbol, signal.direction, fill_price, fill_size);
                } else {
                    std::cout << "[BURST-ADAPTER] Entry FAILED: " << error << "\n";
                    // Reset pending state
                    pending_entry_dir_ = Direction::NONE;
                }
            }
        );
    }
    
    void on_exit_signal(const BurstExitSignal& signal) {
        std::cout << "[BURST-ADAPTER] Exit signal: " << symbol_str(signal.symbol)
                  << " reason=" << exit_str(signal.reason)
                  << " @ " << std::fixed << std::setprecision(2) << signal.exit_price << "\n";
        
        // Get current position to determine exit side
        auto pos = engine_->get_position(signal.symbol);
        if (!pos) {
            std::cout << "[BURST-ADAPTER] No position to exit\n";
            return;
        }
        
        if (!order_sender_) {
            // Shadow mode - simulate fill
            std::cout << "[BURST-ADAPTER] Shadow mode - simulating exit fill\n";
            engine_->on_exit_fill(signal.symbol, signal.exit_price, signal.reason);
            return;
        }
        
        // Live mode - send order via WebSocket API
        std::string binance_symbol = symbol_to_binance(signal.symbol);
        std::string side = (pos->direction == Direction::LONG) ? "SELL" : "BUY";
        
        order_sender_->send_market_order(
            binance_symbol,
            side,
            pos->size,
            [this, signal](bool success, double fill_price, double fill_size, const std::string& error) {
                if (success) {
                    std::cout << "[BURST-ADAPTER] Exit filled @ " << fill_price << "\n";
                    engine_->on_exit_fill(signal.symbol, fill_price, signal.reason);
                } else {
                    std::cout << "[BURST-ADAPTER] Exit FAILED: " << error << "\n";
                    // Force exit on next tick
                }
            }
        );
    }
    
    void on_trade_result(const BurstTradeResult& result) {
        // Log trade result for analysis
        std::cout << "[BURST-ADAPTER] Trade result: " << symbol_str(result.symbol)
                  << " PnL=$" << std::fixed << std::setprecision(2) << result.pnl_usd
                  << " (" << std::setprecision(2) << result.pnl_r << "R)"
                  << " hold=" << result.hold_duration_ms << "ms\n";
        
        // Could write to file/database here for post-mortem analysis
    }
    
    void on_idle_log(BurstSymbol symbol, const GateStatus& status) {
        // Already logged by engine, but could add additional logging here
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // UTILITY
    // ═══════════════════════════════════════════════════════════════════════
    
    static uint64_t now_us() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // MEMBER VARIABLES
    // ═══════════════════════════════════════════════════════════════════════
    
    std::shared_ptr<CryptoBurstEngine> engine_;
    std::shared_ptr<BinanceOrderSender> order_sender_;
    
    // Pending entry state
    BurstSymbol pending_entry_symbol_;
    Direction pending_entry_dir_;
    double pending_entry_size_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// FACTORY FUNCTION
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Create burst engine with BTC-only configuration (recommended)
 */
inline std::shared_ptr<BurstBinanceAdapter> create_btc_burst_adapter(
    std::shared_ptr<BinanceOrderSender> order_sender = nullptr
) {
    auto config = BurstEngineConfig::btc_only();
    auto engine = std::make_shared<CryptoBurstEngine>(config);
    return std::make_shared<BurstBinanceAdapter>(engine, order_sender);
}

} // namespace burst
} // namespace crypto
} // namespace chimera
