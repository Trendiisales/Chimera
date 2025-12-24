// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/SymbolThread.hpp - v6.90
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <thread>
#include <atomic>
#include <array>
#include <memory>
#include <cmath>

#include "BinanceConfig.hpp"
#include "BinanceParser.hpp"
#include "OrderBook.hpp"

#include "../TickCore.hpp"
#include "../core/GlobalKill.hpp"
#include "../ExecutionGate.hpp"
#include "../micro/CentralMicroEngine.hpp"
#include "../signal/SignalAggregator.hpp"
#include "../regime/RegimeClassifier.hpp"
#include "../strategy/MultiStrategyCoordinator.hpp"
#include "../risk/DailyLossGuard.hpp"

namespace Chimera {
namespace Binance {

struct OrderIntent {
    uint16_t symbol_id;
    Side     side;
    double   quantity;
    double   price;
    uint64_t ts_ns;
    uint32_t strategy_id;
};

template<size_t N>
class OrderQueue {
public:
    OrderQueue() noexcept : head_(0), tail_(0) {}
    
    [[nodiscard]] bool push(const OrderIntent& intent) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) % N;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        buffer_[head] = intent;
        head_.store(next, std::memory_order_release);
        return true;
    }
    
    [[nodiscard]] bool pop(OrderIntent& out) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = buffer_[tail];
        tail_.store((tail + 1) % N, std::memory_order_release);
        return true;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::array<OrderIntent, N> buffer_;
};

enum class SymbolState : uint8_t {
    INIT = 0, WAITING = 1, SYNCING = 2, RUNNING = 3, STOPPED = 4, ERROR = 5
};

class SymbolThread {
public:
    SymbolThread(
        const SymbolConfig& config,
        GlobalKill& global_kill,
        DailyLossGuard& daily_loss,
        OrderQueue<256>& order_queue
    ) noexcept
        : config_(config)
        , global_kill_(global_kill)
        , daily_loss_(daily_loss)
        , order_queue_(order_queue)
        , running_(false)
        , state_(SymbolState::INIT)
        , exec_gate_(global_kill, daily_loss, ExecutionGate::Config{
            get_max_position(config.id), 10, 50'000'000, 0.10, 3'000'000'000
          })
    {
        book_.symbol_id = config.id;
    }
    
    ~SymbolThread() { stop(); }
    SymbolThread(const SymbolThread&) = delete;
    SymbolThread& operator=(const SymbolThread&) = delete;
    
    void start() noexcept {
        if (running_.load()) return;
        running_.store(true);
        state_ = SymbolState::WAITING;
        thread_ = std::thread(&SymbolThread::run, this);
    }
    
    void stop() noexcept {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
        state_ = SymbolState::STOPPED;
    }
    
    void on_depth(const DepthUpdate& update) noexcept {
        // v6.93: Use full snapshot for @depth20 stream
        book_.set_full_depth(update.bids.data(), update.bid_count,
                             update.asks.data(), update.ask_count);
        book_.last_update_id = update.last_update_id;
        
        uint64_t now = get_monotonic_ns();
        pending_tick_ = TickCore::make(
            config_.id, Venue::BINANCE,
            static_cast<uint32_t>(update.last_update_id & 0xFFFFFFFF),
            book_.best_bid(), book_.best_ask(),
            book_.best_bid_qty(), book_.best_ask_qty(), now
        );
        has_pending_tick_.store(true, std::memory_order_release);
        ++tick_count_;
        last_tick_ts_ = now;
    }
    
    void on_trade(const TradeUpdate& trade) noexcept {
        last_trade_price_ = trade.price;
        last_trade_qty_ = trade.quantity;
        last_trade_is_buy_ = !trade.is_buyer_maker;
        ++trade_count_;
    }
    
    void set_snapshot(const DepthUpdate& snapshot) noexcept {
        for (uint8_t i = 0; i < snapshot.bid_count; ++i)
            book_.update_bid(snapshot.bids[i].price, snapshot.bids[i].quantity);
        for (uint8_t i = 0; i < snapshot.ask_count; ++i)
            book_.update_ask(snapshot.asks[i].price, snapshot.asks[i].quantity);
        book_.last_update_id = snapshot.last_update_id;
        state_ = SymbolState::RUNNING;
        std::cout << "[" << config_.symbol << "] Snapshot applied, state=RUNNING\n";
    }
    
    [[nodiscard]] const SymbolConfig& config() const noexcept { return config_; }
    [[nodiscard]] SymbolState state() const noexcept { return state_; }
    [[nodiscard]] uint64_t tick_count() const noexcept { return tick_count_; }
    [[nodiscard]] uint64_t trade_count() const noexcept { return trade_count_; }
    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }

private:
    void run() noexcept {
        while (running_.load(std::memory_order_relaxed)) {
            if (global_kill_.killed()) { state_ = SymbolState::STOPPED; break; }
            if (has_pending_tick_.load(std::memory_order_acquire)) {
                process_tick(pending_tick_);
                has_pending_tick_.store(false, std::memory_order_release);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
    }
    
    void process_tick(const TickCore& tick) noexcept {
        if (state_ != SymbolState::RUNNING) return;
        if (!tick.valid()) return;
        
        micro_engine_.on_book(tick.bid, tick.bid_qty, tick.ask, tick.ask_qty, tick.local_ts_ns);
        micro_engine_.on_trade(last_trade_is_buy_, last_trade_qty_, tick.local_ts_ns);
        micro_engine_.on_price(tick.mid, tick.local_ts_ns);
        
        SignalVector sig = signal_agg_.aggregate(micro_engine_.snapshot());
        MarketRegime regime = regime_classifier_.classify(sig);
        MultiStrategyDecision decision = coordinator_.decide(sig, regime);
        
        uint64_t now = get_monotonic_ns();
        GateDecision gate = exec_gate_.check(decision.confidence, tick.local_ts_ns, now);
        
        // v6.90: PROOF-OF-LIFE LOGGING - every 50 ticks
        if (tick_count_ % 50 == 0) {
            const char* intent_str = 
                decision.intent == StrategyIntent::LONG ? "LONG" :
                decision.intent == StrategyIntent::SHORT ? "SHORT" : "FLAT";
            std::cout << "[SIG-" << config_.symbol << "] "
                      << "t=" << tick_count_
                      << " obi=" << sig.obi
                      << " tfi=" << sig.tfi
                      << " raw=" << decision.raw_signal
                      << " norm=" << decision.norm_signal
                      << " DIR=" << intent_str
                      << " conf=" << decision.confidence
                      << " gate=" << (gate.allowed ? "OK" : to_string(gate.reason))
                      << "\n";
        }
        
        // v6.90: Log EVERY non-FLAT intent
        if (decision.intent != StrategyIntent::FLAT) {
            non_flat_count_++;
            std::cout << "[INTENT-" << config_.symbol << "] #" << non_flat_count_
                      << " raw=" << decision.raw_signal
                      << " norm=" << decision.norm_signal
                      << " DIR=" << (decision.intent == StrategyIntent::LONG ? "+1" : "-1")
                      << " gate=" << (gate.allowed ? "OK" : to_string(gate.reason))
                      << "\n";
        }
        
        if (gate.allowed && decision.intent != StrategyIntent::FLAT && decision.confidence > 0.1) {
            OrderIntent intent;
            intent.symbol_id = config_.id;
            intent.side = (decision.intent == StrategyIntent::LONG) ? Side::Buy : Side::Sell;
            intent.quantity = calculate_order_size(tick, decision.confidence);
            intent.price = 0;
            intent.ts_ns = now;
            intent.strategy_id = decision.dominant_strategy;
            
            std::cout << "\n*** [CRYPTO-TRADE] " << config_.symbol 
                      << " " << (intent.side == Side::Buy ? "BUY" : "SELL")
                      << " qty=" << intent.quantity
                      << " conf=" << decision.confidence
                      << " norm=" << decision.norm_signal << " ***\n\n";
            
            if (order_queue_.push(intent)) {
                exec_gate_.on_order_sent(now);
                orders_generated_++;
            }
        }
    }
    
    [[nodiscard]] static double normalize_qty(double qty, double minQty, double stepSize) noexcept {
        if (qty < minQty) qty = minQty;
        double steps = std::floor(qty / stepSize);
        double norm = steps * stepSize;
        if (norm < minQty) norm = minQty;
        return norm;
    }
    
    [[nodiscard]] double calculate_order_size(const TickCore& tick, double confidence) const noexcept {
        double max_pos = get_max_position(config_.id);
        double raw_size = max_pos * confidence;
        double size = normalize_qty(raw_size, config_.lot_size, config_.lot_size);
        if (size * tick.mid < config_.min_notional) {
            double min_size = config_.min_notional / tick.mid;
            size = normalize_qty(min_size, config_.lot_size, config_.lot_size);
        }
        return size;
    }
    
    [[nodiscard]] static double get_max_position(uint16_t symbol_id) noexcept {
        switch (symbol_id) {
            case 1: return TradingParams::MAX_POSITION_BTC;
            case 2: return TradingParams::MAX_POSITION_ETH;
            case 3: return TradingParams::MAX_POSITION_SOL;
            default: return 0.0;
        }
    }
    
    [[nodiscard]] static uint64_t get_monotonic_ns() noexcept {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
    
    const SymbolConfig& config_;
    GlobalKill& global_kill_;
    DailyLossGuard& daily_loss_;
    OrderQueue<256>& order_queue_;
    
    std::thread thread_;
    std::atomic<bool> running_;
    SymbolState state_;
    OrderBook book_;
    
    alignas(64) TickCore pending_tick_;
    alignas(64) std::atomic<bool> has_pending_tick_{false};
    
    double last_trade_price_ = 0.0;
    double last_trade_qty_ = 0.0;
    bool last_trade_is_buy_ = false;
    
    BinanceCentralMicro micro_engine_;
    SignalAggregator signal_agg_;
    RegimeClassifier regime_classifier_;
    MultiStrategyCoordinator coordinator_;
    ExecutionGate exec_gate_;
    
    uint64_t tick_count_ = 0;
    uint64_t trade_count_ = 0;
    uint64_t last_tick_ts_ = 0;
    uint64_t orders_generated_ = 0;
    uint64_t non_flat_count_ = 0;
};

} // namespace Binance
} // namespace Chimera
