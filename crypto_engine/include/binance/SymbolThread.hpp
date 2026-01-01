// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/SymbolThread.hpp - v4.3.2 Warmup Gate Fix
// ═══════════════════════════════════════════════════════════════════════════════
// v4.3.2: CRITICAL FIX - Proper warmup enforcement
//         - Default regime → TRANSITION (was STABLE, allowing immediate trades)
//         - Added 500-tick minimum before ANY trading
//         - No trades until: warmup_complete && regime==STABLE && bootstrap_complete
// v4.3.1: Added regime_ok check (but default was still STABLE - didn't work)
// v4.2.2: CRITICAL FIX - Gate on RAW edge, not scaled edge
//         - compute_projected_edge returns RAW (no vol cap)
//         - AllowTradeHFT gates on raw_edge_bps
//         - Vol cap only affects sizing (post-gate)
// v4.2: AllowTradeHFT gate enforces:
//       - Hard edge-vs-cost invariant (edge >= cost * mult)
//       - Volatility-capped edge projection (no hallucinated edge)
//       - Chop kill switch (displacement >= chop_floor)
//       - Noise-aware SL floor (SL >= 2× spread + buffer)
//       - Cooldown after loss (300ms)
//       - Trade frequency limit (1 per 2s window)
// v4.0: PURE HFT - Warmup = feed sanity ONLY, no prediction for entries
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <thread>
#include <atomic>
#include <array>
#include <memory>
#include <cmath>
#include <cstring>  // v4.2.2: for strcmp
#include <iomanip>  // v7.13: For setprecision
#include <functional>  // v4.2.2: For shadow trade callback

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
#include "../risk/ExpectancyTracker.hpp"     // v7.14: Expectancy + Regime enums
#include "../risk/ExpectancyAuthority.hpp"   // v7.14: Dual-horizon expectancy
#include "../control/HysteresisGate.hpp"     // v7.14: Stable state transitions
#include "../bootstrap/BootstrapEvaluator.hpp" // v4.2.4: Information-based bootstrap
#include "SymbolEnabledManager.hpp"          // v4.3.2: GUI symbol enable/disable

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
        OrderQueue<256>& order_queue,
        bool is_testnet = false  // v7.11: Testnet mode
    ) noexcept
        : config_(config)
        , global_kill_(global_kill)
        , daily_loss_(daily_loss)
        , order_queue_(order_queue)
        , running_(false)
        , state_(SymbolState::INIT)
        , is_testnet_(is_testnet)
        , exec_gate_(global_kill, daily_loss, 
            is_testnet ? ExecutionGate::Config{
                get_max_position(config.id), 10, 50'000'000, 0.05, 5'000'000'000  // v7.11: Lower conf, longer stale
            } : ExecutionGate::Config{
                get_max_position(config.id), 10, 50'000'000, 0.10, 3'000'000'000
            })
        , bootstrap_(config.symbol)  // v4.2.4: Information-based bootstrap
    {
        book_.symbol_id = config.id;
        if (is_testnet_) {
            std::cout << "[" << config.symbol << "] TESTNET MODE - relaxed thresholds\n";
        }
        
        // v4.2.2: Set per-symbol confirmation window based on liquidity
        // BTC/ETH: deep books, fast info propagation = shorter confirmation
        // Alts: thinner, noisier = longer confirmation
        if (strcmp(config.symbol, "BTCUSDT") == 0 || strcmp(config.symbol, "ETHUSDT") == 0) {
            edge_confirm_ns_ = 12'000'000;  // 12ms for majors
        } else if (strcmp(config.symbol, "SOLUSDT") == 0 || strcmp(config.symbol, "AVAXUSDT") == 0) {
            edge_confirm_ns_ = 15'000'000;  // 15ms for large alts
        } else {
            edge_confirm_ns_ = 18'000'000;  // 18ms for smaller alts
        }
        std::cout << "[" << config.symbol << "] edge_confirm_ns = " 
                  << (edge_confirm_ns_ / 1'000'000) << "ms\n";
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
                std::cerr << "[SymbolThread] " << config_.symbol << " join timeout, detaching\n";
                joiner.detach();
            }
        }
        state_ = SymbolState::STOPPED;
    }
    
    void on_depth(const DepthUpdate& update) noexcept {
        // v4.2.2: DIAGNOSTIC - log first few depth updates
        static thread_local uint64_t depth_log_count = 0;
        if (depth_log_count < 5 && update.bid_count > 0 && update.ask_count > 0) {
            std::cout << "[DEPTH-DBG-" << config_.symbol << "] "
                      << "bids=" << (int)update.bid_count << " asks=" << (int)update.ask_count
                      << " bid[0]=" << update.bids[0].price << "@" << update.bids[0].quantity
                      << " ask[0]=" << update.asks[0].price << "@" << update.asks[0].quantity
                      << "\n";
            depth_log_count++;
        }
        
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
    
    // v7.12: Handle bookTicker for REAL-TIME best bid/ask updates
    // This is the FASTEST stream - fires on EVERY price change!
    void on_book_ticker(const BookTickerUpdate& ticker) noexcept {
        // v4.2.2: DIAGNOSTIC - log first few book tickers to verify data
        static thread_local uint64_t ticker_log_count = 0;
        if (ticker_log_count < 5) {
            std::cout << "[TICKER-DBG-" << config_.symbol << "] "
                      << "bid=" << ticker.best_bid << " ask=" << ticker.best_ask
                      << " bid_qty=" << ticker.best_bid_qty << " ask_qty=" << ticker.best_ask_qty
                      << "\n";
            ticker_log_count++;
        }
        
        // Update order book with just the top-of-book
        book_.set_top_of_book(ticker.best_bid, ticker.best_bid_qty,
                              ticker.best_ask, ticker.best_ask_qty);
        
        uint64_t now = get_monotonic_ns();
        pending_tick_ = TickCore::make(
            config_.id, Venue::BINANCE,
            static_cast<uint32_t>(ticker.update_id & 0xFFFFFFFF),
            ticker.best_bid, ticker.best_ask,
            ticker.best_bid_qty, ticker.best_ask_qty, now
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
    
    // ═══════════════════════════════════════════════════════════════════════════
    // v7.14: EXPECTANCY + REGIME STATE (for external tracking and GUI)
    // ═══════════════════════════════════════════════════════════════════════════
    
    // Called when a trade closes to update expectancy
    void record_trade_pnl(double pnl_bps) noexcept {
        expectancy_trades_++;
        
        // v7.14: Update DUAL-HORIZON authority (this is the core system)
        expectancy_authority_.record(pnl_bps);
        
        // Legacy single-horizon update for compatibility
        double alpha = 2.0 / (50 + 1);
        current_expectancy_bps_ = alpha * pnl_bps + (1.0 - alpha) * current_expectancy_bps_;
        
        // Log every 10 trades with full authority status
        if (expectancy_trades_ % 10 == 0) {
            auto decision = expectancy_authority_.decide();
            std::cout << "[AUTHORITY-" << config_.symbol << "] "
                      << "fast=" << expectancy_authority_.fast_expectancy() << "bps"
                      << "(" << expectancy_authority_.fast_trades() << "t) "
                      << "slow=" << expectancy_authority_.slow_expectancy() << "bps"
                      << "(" << expectancy_authority_.slow_trades() << "t) "
                      << "decision=" << Chimera::Risk::decision_str(decision)
                      << " mult=" << expectancy_authority_.size_multiplier() << "x\n";
        }
        
        // v7.14: Use DUAL-HORIZON for disable decision
        // Fast can pause, only SLOW can disable
        auto decision = expectancy_authority_.decide();
        
        if (decision == Chimera::Risk::ExpectancyAuthority::Decision::DISABLED) {
            std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
            std::cout << "║  🔴 AUTO-DISABLE: " << config_.symbol << "\n";
            std::cout << "║  SLOW Expectancy: " << expectancy_authority_.slow_expectancy() << " bps < 0\n";
            std::cout << "║  FAST Expectancy: " << expectancy_authority_.fast_expectancy() << " bps\n";
            std::cout << "║  Slow Trades: " << expectancy_authority_.slow_trades() << "\n";
            std::cout << "║  SLOW has authority - symbol disabled\n";
            std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
        } else if (decision == Chimera::Risk::ExpectancyAuthority::Decision::PAUSED) {
            std::cout << "[PAUSED-" << config_.symbol << "] "
                      << "Fast E=" << expectancy_authority_.fast_expectancy() << "bps < -0.1"
                      << " → Entries paused (slow still positive)\n";
        }
    }
    
    // Getters for GUI/monitoring
    [[nodiscard]] Chimera::Crypto::MarketRegime current_regime() const noexcept { return current_regime_; }
    [[nodiscard]] double current_expectancy_bps() const noexcept { return current_expectancy_bps_; }
    [[nodiscard]] int expectancy_trades() const noexcept { return expectancy_trades_; }
    [[nodiscard]] double spread_ewma() const noexcept { return spread_ewma_; }
    [[nodiscard]] double book_flip_rate() const noexcept { return book_flip_rate_; }
    
    // v7.14: Authority getters
    [[nodiscard]] const Chimera::Risk::ExpectancyAuthority& authority() const noexcept { 
        return expectancy_authority_; 
    }
    [[nodiscard]] double authority_size_mult() const noexcept { 
        return expectancy_authority_.size_multiplier(); 
    }
    
    // v3.0: Additional getters for GUI
    [[nodiscard]] double expectancy_authority_fast() const noexcept {
        return expectancy_authority_.fast_expectancy();
    }
    [[nodiscard]] const char* regime_str() const noexcept {
        return Chimera::Crypto::regime_str(current_regime_);
    }
    [[nodiscard]] uint64_t shadow_trades() const noexcept {
        return shadow_trades_total_;
    }
    
    // v4.2.2: Block reason getter for GUI diagnostics
    [[nodiscard]] const char* last_block_reason_str() const noexcept {
        return block_reason_str(last_block_reason_);
    }
    
    // v4.2.4: Bootstrap status accessors
    [[nodiscard]] bool bootstrap_complete() const noexcept {
        return bootstrap_.is_complete();
    }
    [[nodiscard]] const char* bootstrap_state_str() const noexcept {
        return bootstrap_.state_string();
    }
    [[nodiscard]] uint32_t bootstrap_intents() const noexcept {
        return bootstrap_.intent_count();
    }
    [[nodiscard]] double bootstrap_churn() const noexcept {
        return bootstrap_.churn_rate() * 100.0;  // Return as percentage
    }
    [[nodiscard]] double bootstrap_persistence() const noexcept {
        return bootstrap_.persistence() * 100.0;  // Return as percentage
    }
    
    // Manual expectancy reset (use with caution)
    void reset_expectancy() noexcept {
        current_expectancy_bps_ = 0.3;
        expectancy_trades_ = 0;
        expectancy_authority_.reset();
        std::cout << "[EXPECTANCY-" << config_.symbol << "] Reset to initial state\n";
    }
    
    // v4.2.2: Shadow trade callback for GUI/logging
    using ShadowTradeCallback = std::function<void(const char* symbol, int8_t side, double qty, double price, double pnl_bps)>;
    
    void setShadowTradeCallback(ShadowTradeCallback cb) {
        shadow_trade_callback_ = std::move(cb);
    }

private:
    void run() noexcept {
        std::cout << "[RUN-" << config_.symbol << "] Thread started\n";
        std::cout.flush();
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
        // v3.4: One-time state diagnostic at startup
        if (tick_count_ == 0) {
            std::cout << "[STATE-" << config_.symbol << "] state=" 
                      << static_cast<int>(state_) << " (0=INIT,3=RUNNING) "
                      << "← if 0, snapshot not applied yet\n";
            std::cout.flush();
        }
        
        // v3.4: CRITICAL FIX - Always increment tick counter and process shadow
        // Shadow must run even before full initialization to bootstrap expectancy
        tick_count_++;
        
        // v3.11: Log invalid ticks to diagnose why crypto wasn't generating trades
        if (!tick.valid()) {
            if (tick_count_ <= 5 || tick_count_ % 1000 == 0) {
                std::cout << "[INVALID-TICK-" << config_.symbol << "] "
                          << "bid=" << tick.bid << " ask=" << tick.ask
                          << " (requires ask >= bid > 0)\n";
            }
            return;
        }
        
        // v7.13: Calculate spread and imbalance
        double spread_bps = 0.0;
        double imbalance = 0.0;
        if (tick.ask > 0 && tick.bid > 0) {
            double mid = (tick.bid + tick.ask) * 0.5;
            spread_bps = (tick.ask - tick.bid) / mid * 10000.0;
            
            // Order-book imbalance (critical for HFT)
            double total_qty = tick.bid_qty + tick.ask_qty;
            if (total_qty > 0) {
                imbalance = (tick.bid_qty - tick.ask_qty) / total_qty;
            }
        } else {
            // v4.2.2: DIAGNOSTIC - log when book is empty (spread=0 bug)
            if (tick_count_ <= 10 || tick_count_ % 1000 == 0) {
                std::cout << "[BOOK-EMPTY-" << config_.symbol << "] tick=" << tick_count_
                          << " bid=" << tick.bid << " ask=" << tick.ask
                          << " bid_qty=" << tick.bid_qty << " ask_qty=" << tick.ask_qty
                          << " ← DATA FEED ISSUE\n";
            }
            // Use precomputed imbalance from tick (quantities may still be valid)
            imbalance = tick.imbalance;
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // v4.2.4: INFORMATION-BASED BOOTSTRAP
        // Feed data to bootstrap evaluator on every tick
        // ═══════════════════════════════════════════════════════════════════════
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        bootstrap_.observe_tick(spread_bps, tick.bid, tick.ask, now_ns);
        bootstrap_.observe_safety(true, true, true);  // Guards always active
        
        // ═══════════════════════════════════════════════════════════════════════
        // v7.14: REGIME CLASSIFICATION (run every 100 ticks to avoid overhead)
        // This determines if we are in STABLE/TRANSITION/TOXIC
        // ═══════════════════════════════════════════════════════════════════════
        if (tick_count_ % 100 == 0 || tick_count_ < 10) {
            // Update spread EWMA and std
            double alpha = 0.1;
            double old_ewma = spread_ewma_;
            spread_ewma_ = alpha * spread_bps + (1.0 - alpha) * spread_ewma_;
            
            // Simple std approximation
            double diff = spread_bps - old_ewma;
            spread_std_ = alpha * std::abs(diff) + (1.0 - alpha) * spread_std_;
            
            // Track book flip rate (top-of-book churn)
            // v3.11 FIX: Removed static - was shared across ALL threads causing race conditions!
            bool flipped = (tick.bid != last_best_bid_) || (tick.ask != last_best_ask_);
            book_flip_rate_ = alpha * (flipped ? 1.0 : 0.0) + (1.0 - alpha) * book_flip_rate_;
            last_best_bid_ = tick.bid;
            last_best_ask_ = tick.ask;
            
            // REGIME CLASSIFICATION RULES (these are non-negotiable)
            // Get symbol-specific spread threshold
            double max_spread_for_regime = 2.5;  // BTC default
            if (config_.id == 2) max_spread_for_regime = 3.0;  // ETH
            if (config_.id == 3) max_spread_for_regime = 4.0;  // SOL
            
            // v7.14: Raw regime determination
            Chimera::Crypto::MarketRegime raw_regime;
            bool is_toxic = (spread_bps > max_spread_for_regime * 1.5 || 
                            spread_std_ > 1.5 || 
                            book_flip_rate_ > 0.3);
            bool is_transition = (spread_bps > max_spread_for_regime || 
                                  spread_std_ > 1.0 ||
                                  book_flip_rate_ > 0.2);
            
            // v7.14: TOXIC requires 2 consecutive signals (hysteresis)
            bool confirmed_toxic = toxic_hysteresis_.update(is_toxic);
            
            if (confirmed_toxic) {
                raw_regime = Chimera::Crypto::MarketRegime::TOXIC;
            } else if (is_transition) {
                raw_regime = Chimera::Crypto::MarketRegime::TRANSITION;
            } else {
                raw_regime = Chimera::Crypto::MarketRegime::STABLE;
            }
            
            // v7.14: Time-based hysteresis - regime must hold for 2s minimum
            uint64_t now_ms = tick.local_ts_ns / 1000000;
            bool should_change = (raw_regime != current_regime_);
            bool change_allowed = regime_hysteresis_.update(should_change, now_ms);
            
            Chimera::Crypto::MarketRegime new_regime = change_allowed ? raw_regime : current_regime_;
            
            // Log regime changes
            if (new_regime != current_regime_) {
                std::cout << "[REGIME-" << config_.symbol << "] "
                          << Chimera::Crypto::regime_str(current_regime_) << " → "
                          << Chimera::Crypto::regime_str(new_regime)
                          << " spread=" << spread_bps << "bps std=" << spread_std_
                          << " flip=" << book_flip_rate_ 
                          << " (hysteresis: " << regime_hysteresis_.time_in_state_ms(now_ms) << "ms)\n";
                std::cout.flush();  // v4.2.2: Force flush for immediate output
            }
            current_regime_ = new_regime;
        }
        
        // v7.13: Update imbalance persistence tracking
        if (std::abs(imbalance) > 0.1) {
            if ((imbalance > 0 && last_imbalance_sign_ > 0) ||
                (imbalance < 0 && last_imbalance_sign_ < 0)) {
                // Same direction - accumulate time
                imbalance_persist_ms_ += 10;  // Approx tick interval
            } else {
                // Direction changed - reset
                imbalance_persist_ms_ = 0;
                last_imbalance_sign_ = (imbalance > 0) ? 1 : -1;
            }
        } else {
            imbalance_persist_ms_ = 0;
            last_imbalance_sign_ = 0;
        }
        
        micro_engine_.on_book(tick.bid, tick.bid_qty, tick.ask, tick.ask_qty, tick.local_ts_ns);
        micro_engine_.on_trade(last_trade_is_buy_, last_trade_qty_, tick.local_ts_ns);
        micro_engine_.on_price(tick.mid, tick.local_ts_ns);
        
        SignalVector sig = signal_agg_.aggregate(micro_engine_.snapshot());
        MarketRegime regime = regime_classifier_.classify(sig);
        MultiStrategyDecision decision = coordinator_.decide(sig, regime);
        
        // v7.13: LIVE MODE - Use order-book imbalance for HFT signals
        // This is the core HFT logic - book pressure drives entries
        if (!is_testnet_) {
            // Get symbol-specific thresholds
            double min_imbalance = 0.18;    // BTCUSDT default
            double max_spread = 2.5;        // bps
            uint64_t min_persist_ms = 80;   // ms
            
            if (config_.id == 2) {  // ETHUSDT
                min_imbalance = 0.20;
                max_spread = 3.0;
                min_persist_ms = 90;
            } else if (config_.id == 3) {  // SOLUSDT
                min_imbalance = 0.25;
                max_spread = 4.0;
                min_persist_ms = 100;
            }
            
            // ENTRY GATES - ALL must pass
            // v3.13: Allow zero spread (was > 0.1)
            bool spread_ok = spread_bps <= max_spread && spread_bps >= 0.0;
            bool imbalance_ok = std::abs(imbalance) >= min_imbalance;
            bool persist_ok = imbalance_persist_ms_ >= min_persist_ms;
            // v4.2.2: Reduced from 10 to 5 levels - @bookTicker often provides fewer
            bool book_ok = book_.bid_levels() >= 5 && book_.ask_levels() >= 5;
            
            // v7.13: Generate HFT signal from order-book pressure
            if (spread_ok && imbalance_ok && persist_ok && book_ok) {
                if (imbalance > min_imbalance) {
                    decision.intent = StrategyIntent::LONG;
                    decision.confidence = std::min(1.0, 0.5 + imbalance);
                    decision.norm_signal = imbalance;
                } else if (imbalance < -min_imbalance) {
                    decision.intent = StrategyIntent::SHORT;
                    decision.confidence = std::min(1.0, 0.5 + std::abs(imbalance));
                    decision.norm_signal = imbalance;
                }
            }
            
            // v3.4: HFT debug removed to prevent output flooding
        } else {
            // TESTNET MODE - simpler momentum signals
            double price_delta = tick.mid - last_mid_;
            last_mid_ = tick.mid;
            momentum_ema_ = 0.5 * price_delta + 0.5 * momentum_ema_;
            double mom_signal = momentum_ema_ / (tick.mid * 0.0001);
            
            constexpr double TESTNET_MOM_THRESHOLD = 0.15;
            if (mom_signal > TESTNET_MOM_THRESHOLD) {
                decision.intent = StrategyIntent::LONG;
                decision.confidence = std::min(1.0, 0.5 + mom_signal / 4.0);
                decision.norm_signal = mom_signal;
            } else if (mom_signal < -TESTNET_MOM_THRESHOLD) {
                decision.intent = StrategyIntent::SHORT;
                decision.confidence = std::min(1.0, 0.5 + (-mom_signal) / 4.0);
                decision.norm_signal = mom_signal;
            }
        }
        
        uint64_t now = get_monotonic_ns();
        GateDecision gate = exec_gate_.check(decision.confidence, tick.local_ts_ns, now);
        
        // v3.4: SIG debug removed to prevent output flooding
        
        // v3.4: INTENT debug removed to prevent output flooding
        
        // ═══════════════════════════════════════════════════════════════════════
        // v4.2: PURE HFT SHADOW TRADES with AllowTradeHFT gate
        // This is the CORRECT implementation - no trade without edge > cost
        // ═══════════════════════════════════════════════════════════════════════
        
        // v4.2: Update tracking metrics EVERY tick
        update_displacement_tracking(tick.mid, tick.local_ts_ns);
        update_realized_vol(tick.mid);
        
        // v4.2: Feed sanity (basic prerequisite)
        bool feed_sane = tick.bid > 0 && tick.ask > 0 && tick.bid < tick.ask;
        // v4.2.2: Reduced from 100 to 20 ticks warmup (bootstrap faster)
        bool is_shadow_eligible = feed_sane && tick_count_ > 20;
        
        // v4.2: Compute momentum for edge calculation
        double price_delta = tick.mid - last_mid_;
        last_mid_ = tick.mid;
        momentum_ema_ = 0.7 * price_delta + 0.3 * momentum_ema_;
        double momentum_bps = momentum_ema_ / (tick.mid * 0.0001);
        
        // v4.2.2: Update micro-trend for directional bias filter
        micro_trend_ema_ = 0.05 * price_delta + 0.95 * micro_trend_ema_;
        
        // v4.2: Entry direction from imbalance (but gate decides if we can trade)
        double shadow_imbalance_threshold = 0.10;  // Lower threshold - gate handles rest
        if (config_.id == 2) shadow_imbalance_threshold = 0.12;
        if (config_.id == 3) shadow_imbalance_threshold = 0.15;
        
        int shadow_direction = (imbalance > shadow_imbalance_threshold) ? 1 :
                               (imbalance < -shadow_imbalance_threshold) ? -1 : 0;
        
        // v4.2: Compute edge and get metrics for gate
        double projected_edge = compute_projected_edge(imbalance, momentum_bps);
        double displacement = get_displacement_bps();
        
        // v4.2: Debug every 100 ticks - now shows gate metrics
        // v4.3.1: Added regime to output
        // v4.3.2: Added warmup status + symbol enabled
        // v4.3.4: Added state (INIT/RUNNING)
        if (tick_count_ == 1 || tick_count_ % 100 == 0) {
            bool state_ok = (state_ == SymbolState::RUNNING);
            bool warmup_ok = (tick_count_ >= 500);
            bool sym_enabled = Chimera::isSymbolTradingEnabled(config_.symbol);
            std::cout << "[HFT-" << config_.symbol << "] "
                      << "t=" << tick_count_
                      << " ST=" << (state_ok ? "RUN" : "INIT")
                      << " EN=" << (sym_enabled ? "Y" : "N")
                      << " WARMUP=" << (warmup_ok ? "OK" : "WAIT")
                      << " REGIME=" << Chimera::Crypto::regime_str(current_regime_)
                      << " spread=" << std::fixed << std::setprecision(2) << spread_bps
                      << " edge=" << std::setprecision(1) << projected_edge
                      << " vol=" << std::setprecision(1) << realized_vol_bps_
                      << " disp=" << std::setprecision(1) << displacement
                      << " imb=" << std::setprecision(3) << imbalance
                      << " dir=" << shadow_direction
                      << " pos=" << (shadow_position_open_ ? "Y" : "N")
                      << " WR=" << std::setprecision(0) 
                      << (shadow_wins_ + shadow_losses_ > 0 
                          ? 100.0 * shadow_wins_ / (shadow_wins_ + shadow_losses_) : 0.0) << "%"
                      << " (" << shadow_wins_ << "/" << shadow_losses_ << ")"
                      << "\n";
            std::cout.flush();
        }
        
        // v4.2: THE CRITICAL GATE - nothing fires without passing this
        // v4.2.2: Now passes direction for counter-trend filter
        // v4.3.1: CRITICAL FIX - Added regime check to prevent churning during INIT/TRANSITION/TOXIC
        // v4.3.2: Added minimum tick count (500) + GUI symbol enable check
        // v4.3.4: CRITICAL FIX - Must be in RUNNING state (not INIT/WAITING/SYNCING)
        bool state_running = (state_ == SymbolState::RUNNING);  // v4.3.4: THE KEY CHECK
        bool symbol_enabled = Chimera::isSymbolTradingEnabled(config_.symbol);  // v4.3.2: GUI control
        bool warmup_complete = (tick_count_ >= 500);  // v4.3.2: Need 500 ticks minimum
        bool regime_ok = (current_regime_ == Chimera::Crypto::MarketRegime::STABLE);
        bool gate_pass = is_shadow_eligible && 
                         state_running &&    // v4.3.4: MUST be RUNNING state
                         symbol_enabled &&   // v4.3.2: Must be enabled in GUI
                         warmup_complete &&  // v4.3.2: Must have 500+ ticks
                         regime_ok &&  // v4.3.1: MUST be STABLE regime
                         shadow_direction != 0 &&
                         !shadow_position_open_ &&
                         AllowTradeHFT(projected_edge, spread_bps, realized_vol_bps_, 
                                       displacement, imbalance, tick.local_ts_ns, shadow_direction);
        
        // ═══════════════════════════════════════════════════════════════════════
        // v4.2.4: RECORD INTENT for bootstrap (even if we don't trade yet)
        // This is the KEY difference - we count SIGNALS, not just EXECUTIONS
        // ═══════════════════════════════════════════════════════════════════════
        if (gate_pass) {
            bootstrap_.observe_intent(
                static_cast<int8_t>(shadow_direction),
                projected_edge,
                spread_bps,
                static_cast<uint8_t>(current_regime_),
                now_ns
            );
        }
        
        // v4.2.4: Evaluate bootstrap state (replaces trade-count bootstrap)
        bool bootstrap_complete = bootstrap_.evaluate();
        
        // ═══════════════════════════════════════════════════════════════════════
        // v4.3.4: NUCLEAR HARD BLOCK - FINAL SAFETY CHECK BEFORE ANY TRADE
        // This CANNOT be bypassed - it's the last line of defense
        // ═══════════════════════════════════════════════════════════════════════
        if (gate_pass && bootstrap_complete) {
            // FINAL CHECKS - abort if ANY fails
            bool final_state_ok = (state_ == SymbolState::RUNNING);
            bool final_symbol_ok = Chimera::isSymbolTradingEnabled(config_.symbol);
            bool final_warmup_ok = (tick_count_ >= 500);
            bool final_regime_ok = (current_regime_ == Chimera::Crypto::MarketRegime::STABLE);
            
            if (!final_state_ok) {
                std::cout << "[HARD-BLOCK-" << config_.symbol << "] STATE NOT RUNNING (" << static_cast<int>(state_) << ") - ABORTING TRADE\n";
                std::cout.flush();
                return;  // ABORT
            }
            if (!final_symbol_ok) {
                std::cout << "[HARD-BLOCK-" << config_.symbol << "] SYMBOL NOT ENABLED - ABORTING TRADE\n";
                std::cout.flush();
                return;  // ABORT
            }
            if (!final_warmup_ok) {
                std::cout << "[HARD-BLOCK-" << config_.symbol << "] WARMUP NOT COMPLETE (t=" << tick_count_ << ") - ABORTING TRADE\n";
                std::cout.flush();
                return;  // ABORT
            }
            if (!final_regime_ok) {
                std::cout << "[HARD-BLOCK-" << config_.symbol << "] REGIME NOT STABLE (" 
                          << Chimera::Crypto::regime_str(current_regime_) << ") - ABORTING TRADE\n";
                std::cout.flush();
                return;  // ABORT
            }
            
            // OPEN shadow position - ALL checks passed
            shadow_position_open_ = true;
            shadow_entry_price_ = tick.mid;
            shadow_entry_ts_ = tick.local_ts_ns;
            shadow_side_ = shadow_direction;
            shadow_trades_total_++;
            trades_in_window_++;  // Track for frequency limit
            
            // v4.2.2: Callback for GUI/logging (entry = 0 pnl)
            // v4.2.2: Increased qty from 0.001 to 0.01 for better PnL visibility
            if (shadow_trade_callback_) {
                shadow_trade_callback_(config_.symbol, shadow_side_, 0.01, tick.mid, 0.0);
            }
            
            // v4.2.2: LOUD ENTRY LOGGING
            std::cout << "\n"
                      << "▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶\n"
                      << "▶▶  ENTRY  " << config_.symbol << "  " << (shadow_side_ > 0 ? "LONG" : "SHORT") << "  @" << std::fixed << std::setprecision(2) << shadow_entry_price_ << "\n"
                      << "▶▶  edge=" << std::setprecision(1) << projected_edge << "bps  spread=" << spread_bps << "bps  disp=" << displacement << "bps  (#" << shadow_trades_total_ << ")\n"
                      << "▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶▶\n\n";
            std::cout.flush();
        }
        
        // Check shadow position exit (TP/SL or max hold time)
        if (shadow_position_open_) {
            double shadow_pnl_bps = 0.0;
            if (shadow_side_ > 0) {
                shadow_pnl_bps = (tick.mid - shadow_entry_price_) / shadow_entry_price_ * 10000.0;
            } else {
                shadow_pnl_bps = (shadow_entry_price_ - tick.mid) / shadow_entry_price_ * 10000.0;
            }
            
            uint64_t hold_ms = (tick.local_ts_ns - shadow_entry_ts_) / 1000000;
            
            // v4.2: WIDER TP/SL - noise-aware (was 3/-4, now 8/-5)
            // TP must be achievable, SL must survive noise
            double SHADOW_TP_BPS = 8.0;     // Realistic target
            double SHADOW_SL_BPS = -5.0;    // Noise-aware stop
            uint64_t SHADOW_MAX_HOLD_MS = 3000;  // 3s max hold (was 2s)
            
            // v4.2: Symbol-specific adjustments
            if (config_.id == 2) {  // ETH - slightly tighter
                SHADOW_TP_BPS = 7.0;
                SHADOW_SL_BPS = -4.5;
            } else if (config_.id == 3) {  // SOL - wider (more volatile)
                SHADOW_TP_BPS = 10.0;
                SHADOW_SL_BPS = -6.0;
                SHADOW_MAX_HOLD_MS = 4000;
            }
            
            // v4.2: Dynamic SL floor based on spread (never tighter than 2× spread)
            double min_sl = -(spread_bps * 2.0 + 1.0);  // At least 2× spread + buffer
            if (SHADOW_SL_BPS > min_sl) {
                SHADOW_SL_BPS = min_sl;
            }
            
            // ═══════════════════════════════════════════════════════════════
            // v4.2.2: WIN RATE IMPROVEMENTS - Asymmetric exits + Slow bleed
            // ═══════════════════════════════════════════════════════════════
            
            // v4.2.2: FAST TP at 60% - Lock small wins quickly (win rate ↑)
            double FAST_TP_BPS = SHADOW_TP_BPS * 0.6;
            
            // v4.2.2 FIX: SLOW BLEED EXIT - Only exit if LOSING after hold time
            // Changed: 800ms→1200ms, 0.5→-1.0 (only exit if losing, not flat)
            static constexpr uint64_t SLOW_BLEED_MS = 1200;
            static constexpr double SLOW_BLEED_THRESHOLD = -1.0;  // Only if losing 1+ bps
            
            bool slow_bleed = (hold_ms > SLOW_BLEED_MS && 
                              shadow_pnl_bps < SLOW_BLEED_THRESHOLD && 
                              shadow_pnl_bps > SHADOW_SL_BPS * 0.5);
            
            bool should_exit = (shadow_pnl_bps >= FAST_TP_BPS) ||   // v4.2.2: Fast TP
                              (shadow_pnl_bps >= SHADOW_TP_BPS) ||  // Full TP
                              (shadow_pnl_bps <= SHADOW_SL_BPS) ||  // SL
                              slow_bleed ||                         // v4.2.2: Slow bleed
                              (hold_ms >= SHADOW_MAX_HOLD_MS);      // Max hold
            
            if (should_exit) {
                // CLOSE shadow position - record PnL
                shadow_position_open_ = false;
                
                // v4.2.2: Callback for GUI/logging (exit with pnl)
                // v4.2.2: Increased qty from 0.001 to 0.01 for better PnL visibility
                if (shadow_trade_callback_) {
                    // Negative side = closing (opposite of entry)
                    shadow_trade_callback_(config_.symbol, -shadow_side_, 0.01, tick.mid, shadow_pnl_bps);
                }
                
                // Feed PnL to expectancy tracker
                record_trade_pnl(shadow_pnl_bps);
                
                // v4.2.2: Enhanced exit reason tracking
                const char* exit_reason = 
                    (shadow_pnl_bps >= SHADOW_TP_BPS) ? "TP" :
                    (shadow_pnl_bps >= FAST_TP_BPS) ? "TP_FAST" :
                    (shadow_pnl_bps <= SHADOW_SL_BPS) ? "SL" :
                    slow_bleed ? "SLOW_BLEED" : "TIME";
                
                // Track win/loss
                // v4.2.2: Scratches (slow bleed near zero) don't count as losses
                bool is_loss = shadow_pnl_bps < -0.5;  // Only real losses
                bool is_win = shadow_pnl_bps > 0.5;    // Only real wins
                
                if (is_win) {
                    shadow_wins_++;
                    wins_today_++;
                } else if (is_loss) {
                    shadow_losses_++;
                    // v4.2: COOLDOWN AFTER LOSS - critical for win rate
                    cooldown_until_ns_ = tick.local_ts_ns + LOSS_COOLDOWN_NS;
                }
                // Scratches (-0.5 to +0.5) don't affect win/loss count
                
                trades_today_++;
                
                double win_rate = (shadow_wins_ + shadow_losses_ > 0) 
                    ? 100.0 * shadow_wins_ / (shadow_wins_ + shadow_losses_) : 0.0;
                
                // v4.2.2: LOUD WIN/LOSS LOGGING
                if (is_win) {
                    std::cout << "\n"
                              << "████████████████████████████████████████████████████████████\n"
                              << "██  ✅ WIN ✅  " << config_.symbol << "  +" << std::fixed << std::setprecision(2) << shadow_pnl_bps << " bps\n"
                              << "██  reason=" << exit_reason << "  hold=" << hold_ms << "ms\n"
                              << "██  WR=" << std::setprecision(0) << win_rate << "%  (" << shadow_wins_ << "W/" << shadow_losses_ << "L)\n"
                              << "████████████████████████████████████████████████████████████\n\n";
                } else if (is_loss) {
                    std::cout << "\n"
                              << "################################################################\n"
                              << "##  ❌ LOSS ❌  " << config_.symbol << "  " << std::fixed << std::setprecision(2) << shadow_pnl_bps << " bps\n"
                              << "##  reason=" << exit_reason << "  hold=" << hold_ms << "ms  [COOLDOWN 300ms]\n"
                              << "##  WR=" << std::setprecision(0) << win_rate << "%  (" << shadow_wins_ << "W/" << shadow_losses_ << "L)\n"
                              << "################################################################\n\n";
                } else {
                    // Scratch
                    std::cout << "[SCRATCH] " << config_.symbol << " " << exit_reason 
                              << " pnl=" << std::fixed << std::setprecision(2) << shadow_pnl_bps << "bps"
                              << " hold=" << hold_ms << "ms\n";
                }
                std::cout.flush();
            }
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // v7.14: HARD REGIME GATE - FOR LIVE TRADES ONLY
        // Shadow trades already processed above - live requires STABLE
        // ═══════════════════════════════════════════════════════════════════════
        if (current_regime_ != Chimera::Crypto::MarketRegime::STABLE) {
            // v3.4: Debug removed - silent return for non-STABLE
            return;  // HARD STOP for LIVE - shadow already processed
        }
        
        // ═══════════════════════════════════════════════════════════════════════
        // v7.14: DUAL-HORIZON AUTHORITY CHECK - FOR LIVE TRADES
        // Authority is bootstrapped by shadow trades above
        // ═══════════════════════════════════════════════════════════════════════
        auto authority_decision = expectancy_authority_.decide();
        
        if (authority_decision == Chimera::Risk::ExpectancyAuthority::Decision::DISABLED ||
            authority_decision == Chimera::Risk::ExpectancyAuthority::Decision::PAUSED) {
            // v3.4: Debug removed - silent return
            return;  // HARD STOP for LIVE
        }
        
        if (gate.allowed && decision.intent != StrategyIntent::FLAT && decision.confidence > 0.1) {
            // v7.14: Estimate latency from tick arrival intervals
            // (TickCore doesn't have exchange timestamp, so we use tick rate as proxy)
            double latency_ms = 0.5;  // Default assumption: 0.5ms is good
            if (last_tick_ts_ > 0) {
                uint64_t tick_interval_ns = tick.local_ts_ns - last_tick_ts_;
                // If ticks are arriving slowly (>10ms apart), assume latency issue
                if (tick_interval_ns > 10000000) {
                    latency_ms = tick_interval_ns / 1000000.0;
                }
            }
            
            OrderIntent intent;
            intent.symbol_id = config_.id;
            intent.side = (decision.intent == StrategyIntent::LONG) ? Side::Buy : Side::Sell;
            
            // v7.14: Full AUM scaling with authority multiplier
            double authority_mult = expectancy_authority_.size_multiplier();
            intent.quantity = calculate_order_size(tick, decision.confidence,
                                                   expectancy_authority_.authority_expectancy(),
                                                   current_regime_,
                                                   latency_ms);
            intent.quantity *= authority_mult;  // Apply authority scaling
            
            // If size came out to 0, don't trade
            if (intent.quantity <= 0) {
                // v3.4: Debug removed - silent return
                return;
            }
            
            intent.price = 0;
            intent.ts_ns = now;
            intent.strategy_id = decision.dominant_strategy;
            
            std::cout << "\n*** [CRYPTO-TRADE] " << config_.symbol 
                      << " " << (intent.side == Side::Buy ? "BUY" : "SELL")
                      << " qty=" << intent.quantity
                      << " conf=" << decision.confidence
                      << " auth=" << Chimera::Risk::decision_str(authority_decision)
                      << " mult=" << authority_mult << "x"
                      << " regime=" << Chimera::Crypto::regime_str(current_regime_)
                      << (is_testnet_ ? " [TESTNET]" : "") << " ***\n\n";
            
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
    
    // ═══════════════════════════════════════════════════════════════════════════
    // v7.14: FULL AUM SCALING - Size follows edge, not confidence
    // ═══════════════════════════════════════════════════════════════════════════
    [[nodiscard]] double calculate_order_size(const TickCore& tick, double confidence,
                                               double expectancy_bps, 
                                               Chimera::Crypto::MarketRegime regime,
                                               double latency_ms) const noexcept {
        double base_size = get_max_position(config_.id);
        
        // ─────────────────────────────────────────────────────────────────────
        // Factor 1: EXPECTANCY (most important - kills losing strategies)
        // ─────────────────────────────────────────────────────────────────────
        double expectancy_factor = 1.0;
        if (expectancy_bps <= 0.0) {
            expectancy_factor = 0.0;  // HARD STOP - no trading
        } else if (expectancy_bps < 0.2) {
            expectancy_factor = 0.5;  // Cautious
        } else if (expectancy_bps < 0.4) {
            expectancy_factor = 1.0;  // Normal
        } else {
            expectancy_factor = 1.5;  // Scaled up (capped)
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Factor 2: REGIME (no trades in TOXIC/TRANSITION)
        // ─────────────────────────────────────────────────────────────────────
        double regime_factor = 1.0;
        switch (regime) {
            case Chimera::Crypto::MarketRegime::STABLE:     regime_factor = 1.0; break;
            case Chimera::Crypto::MarketRegime::TRANSITION: regime_factor = 0.3; break;
            case Chimera::Crypto::MarketRegime::TOXIC:      regime_factor = 0.0; break;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Factor 3: SESSION (trade hard when market pays)
        // ─────────────────────────────────────────────────────────────────────
        double session_factor = get_session_factor();
        
        // ─────────────────────────────────────────────────────────────────────
        // Factor 4: LATENCY (protect edge when slow)
        // ─────────────────────────────────────────────────────────────────────
        double latency_factor = 1.0;
        if (latency_ms <= 1.0)      latency_factor = 1.0;
        else if (latency_ms <= 3.0) latency_factor = 0.6;
        else if (latency_ms <= 5.0) latency_factor = 0.3;
        else                        latency_factor = 0.0;  // Hard block
        
        // ─────────────────────────────────────────────────────────────────────
        // FINAL SIZE CALCULATION
        // ─────────────────────────────────────────────────────────────────────
        double size = base_size 
                    * confidence 
                    * expectancy_factor 
                    * regime_factor 
                    * session_factor 
                    * latency_factor;
        
        // Clamp to reasonable range
        size = std::max(0.0, std::min(size, base_size * 2.0));
        
        // Normalize to lot size
        size = normalize_qty(size, config_.lot_size, config_.lot_size);
        
        // v3.7: Enforce minimum tradeable size
        // If we want to trade at all (size > 0), ensure we meet minimums
        if (size > 0) {
            // Check min_notional ($10 on Binance)
            double min_size_notional = config_.min_notional / tick.mid;
            // Check min lot_size (smallest tradeable increment)
            double min_size_lot = config_.lot_size;
            // Use the larger of the two
            double min_size = std::max(min_size_notional, min_size_lot);
            min_size = normalize_qty(min_size, config_.lot_size, config_.lot_size);
            
            // If calculated size is below minimum, use minimum
            if (size < min_size) {
                size = min_size;
            }
        }
        
        // v3.6-clean: Removed verbose size debug logging
        
        return size;
    }
    
    // Legacy overload for backward compatibility
    [[nodiscard]] double calculate_order_size(const TickCore& tick, double confidence) const noexcept {
        // Use neutral defaults if expectancy/regime not available
        return calculate_order_size(tick, confidence, 0.3, 
                                    Chimera::Crypto::MarketRegime::STABLE, 0.5);
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // v7.14: Session factor based on UTC hour
    // ═══════════════════════════════════════════════════════════════════════════
    [[nodiscard]] static double get_session_factor() noexcept {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm* utc = gmtime(&time_t);
        int hour = utc->tm_hour;
        
        // Crypto session weights (UTC)
        if (hour >= 0 && hour < 2)   return 0.9;   // Asia liquidity burst
        if (hour >= 2 && hour < 7)   return 0.7;   // Asia quiet
        if (hour >= 7 && hour < 9)   return 1.0;   // London open
        if (hour >= 9 && hour < 13)  return 1.1;   // London session
        if (hour >= 13 && hour < 16) return 1.6;   // US equities overlap (BEST)
        if (hour >= 16 && hour < 20) return 1.2;   // NY session
        if (hour >= 20 && hour < 24) return 0.5;   // Dead hours
        return 0.8;
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
    
    // ═══════════════════════════════════════════════════════════════════════════
    // v4.2.2: CRITICAL HFT GATE - The single most important function
    // GOLDEN RULE: Trade EXISTENCE decided on RAW edge. Sizing on scaled edge.
    // WIN RATE IMPROVEMENTS: Edge confirmation, regime filter, directional bias
    // ═══════════════════════════════════════════════════════════════════════════
    [[nodiscard]] bool AllowTradeHFT(
        double projected_edge_bps,  // This is now RAW edge from compute_projected_edge
        double spread_bps,
        [[maybe_unused]] double realized_vol_bps,  // Reserved for post-gate sizing
        double displacement_bps,
        double orderbook_imbalance,  // v4.2: Added for crypto microstructure
        uint64_t now_ns,
        int intended_direction = 0  // v4.2.2: For directional bias check
    ) noexcept {
        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: WIN RATE GATE 1 - SYMBOL SELF-HEALING
        // If rolling win rate < 40% after 5 trades today, disable symbol
        // ═══════════════════════════════════════════════════════════════════
        if (disabled_for_day_) {
            return false;
        }
        
        if (trades_today_ >= 5) {
            double rolling_wr = wins_today_ > 0 ? double(wins_today_) / trades_today_ : 0.0;
            if (rolling_wr < 0.40) {
                disabled_for_day_ = true;
                return false;
            }
        }
        
        // Symbol-specific thresholds
        double max_spread_bps = 1.2;      // BTC default
        double min_edge_mult = 2.5;
        double min_edge_bps = 4.0;
        double slippage_bps = 0.8;
        
        if (config_.id == 2) {  // ETH
            max_spread_bps = 1.5;
            min_edge_mult = 2.3;
            min_edge_bps = 3.5;
            slippage_bps = 1.0;
        } else if (config_.id == 3) {  // SOL
            max_spread_bps = 2.0;
            min_edge_mult = 2.5;
            min_edge_bps = 4.5;
            slippage_bps = 1.2;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.2.4: BOOTSTRAP NOW HANDLED UPSTREAM
        // - Bootstrap is information-based (data ready + edge quality)
        // - No trade-count relaxation needed here
        // - Intent tracking seeds the BootstrapEvaluator
        // - AllowTradeHFT just validates edge quality consistently
        // ═══════════════════════════════════════════════════════════════════
        double effective_edge_mult = min_edge_mult;
        double effective_min_edge = min_edge_bps;
        
        // 1. SPREAD SANITY
        // v4.2.2: Distinguish "book not populated" (spread=0) from "spread too wide"
        if (spread_bps <= 0.0) {
            record_block(BlockReason::BOOK_NOT_READY);
            if (burst_start_ns_ != 0) {
                std::cout << "[BURST-" << config_.symbol << "] RESET (book not populated)\n";
            }
            burst_start_ns_ = 0;
            edge_confirm_start_ns_ = 0;
            return false;
        }
        if (spread_bps > max_spread_bps) {
            record_block(BlockReason::SPREAD_TOO_WIDE);
            if (burst_start_ns_ != 0) {
                std::cout << "[BURST-" << config_.symbol << "] RESET (spread=" << spread_bps << "bps > " << max_spread_bps << ")\n";
            }
            burst_start_ns_ = 0;
            edge_confirm_start_ns_ = 0;
            return false;
        }
        
        // 2. TOTAL COST CALCULATION
        double total_cost_bps = spread_bps + slippage_bps + 0.5;  // +0.5 safety
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: CRITICAL FIX - Compute RAW edge for gating
        // Imbalance boost is part of raw edge, NOT post-processing
        // ═══════════════════════════════════════════════════════════════════
        double imbalance_boost = std::abs(orderbook_imbalance) * 2.0;  // bps scale
        double raw_edge_bps = projected_edge_bps + imbalance_boost;
        
        // EDGE STARVATION DETECTION
        if (raw_edge_bps < 0.01) {
            record_block(BlockReason::EDGE_TOO_LOW);
            return false;
        }
        
        // DIAGNOSTIC: Log edge values periodically
        static thread_local uint64_t edge_log_counter = 0;
        if (++edge_log_counter % 500 == 1) {
            std::cout << "[EDGE-" << config_.symbol << "] raw=" << raw_edge_bps 
                      << " min=" << effective_min_edge
                      << " cost=" << total_cost_bps
                      << " imb=" << orderbook_imbalance
                      << " disp=" << displacement_bps << "\n";
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // HARD GATES ON RAW EDGE (NOT scaled edge)
        // v4.2.2: Trade EXISTENCE decided here - NO scaling applied yet
        // ═══════════════════════════════════════════════════════════════════
        
        // HYSTERESIS: Only reset burst if edge COLLAPSES, not minor dip
        static constexpr double EDGE_RESET_RATIO = 0.6;
        
        // 3. ABSOLUTE EDGE FLOOR (RAW edge) - WITH HYSTERESIS
        if (raw_edge_bps < effective_min_edge * EDGE_RESET_RATIO) {
            // Edge COLLAPSED - hard reset
            record_block(BlockReason::EDGE_TOO_LOW);
            if (burst_start_ns_ != 0) {
                std::cout << "[BURST-" << config_.symbol << "] RESET (edge collapsed)\n";
            }
            burst_start_ns_ = 0;
            edge_confirm_start_ns_ = 0;
            return false;
        }
        
        // Edge below min but above reset threshold - block but DON'T reset timer
        if (raw_edge_bps < effective_min_edge) {
            record_block(BlockReason::EDGE_TOO_LOW);
            // NO RESET - edge may recover
            return false;
        }
        
        // 4. HARD EDGE VS COST (RAW edge - THE INVARIANT)
        if (raw_edge_bps < total_cost_bps * effective_edge_mult) {
            record_block(BlockReason::COST_TOO_HIGH);
            // Only reset if significantly below threshold
            if (raw_edge_bps < total_cost_bps * effective_edge_mult * EDGE_RESET_RATIO) {
                if (burst_start_ns_ != 0) {
                    std::cout << "[BURST-" << config_.symbol << "] RESET (edge << cost)\n";
                }
                burst_start_ns_ = 0;
                edge_confirm_start_ns_ = 0;
            }
            return false;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: WIN RATE GATE 2 - BURST-RELATIVE EDGE CONFIRMATION
        // Confirmation measured from burst start, not absolute time
        // Required = min(edge_confirm_ns_, burst_age * 70%)
        // Crypto bursts are fast - allows late entries while filtering flickers
        // ═══════════════════════════════════════════════════════════════════
        
        // Latch burst start (edge + cost gates passed = burst active)
        if (burst_start_ns_ == 0) {
            burst_start_ns_ = now_ns;
            std::cout << "[BURST-" << config_.symbol << "] START detected\n";
        }
        
        // Edge confirmation starts at burst start, not now
        if (edge_confirm_start_ns_ == 0) {
            edge_confirm_start_ns_ = burst_start_ns_;
        }
        
        uint64_t burst_age_ns = now_ns - burst_start_ns_;
        uint64_t confirm_age_ns = now_ns - edge_confirm_start_ns_;
        
        // Adaptive confirmation: require min(configured, 70% of burst)
        uint64_t required_confirm_ns = std::min(edge_confirm_ns_, burst_age_ns * 7 / 10);
        
        // But enforce minimum 3ms to filter obvious flickers (crypto is faster)
        if (required_confirm_ns < 3'000'000) required_confirm_ns = 3'000'000;
        
        if (confirm_age_ns < required_confirm_ns) {
            record_block(BlockReason::EDGE_CONFIRMING);
            uint64_t confirm_age_ms = confirm_age_ns / 1'000'000;
            uint64_t required_ms = required_confirm_ns / 1'000'000;
            uint64_t burst_age_ms = burst_age_ns / 1'000'000;
            if (confirm_age_ms > 0) {
                std::cout << "[EDGE-CONFIRM-" << config_.symbol << "] waiting " << confirm_age_ms 
                          << "ms / " << required_ms << "ms (burst " << burst_age_ms << "ms)\n";
            }
            return false;
        }
        
        std::cout << "[EDGE-CONFIRM-" << config_.symbol << "] ✓ PASSED after " 
                  << (confirm_age_ns / 1'000'000) << "ms (burst " 
                  << (burst_age_ns / 1'000'000) << "ms)\n";
        
        // ═══════════════════════════════════════════════════════════════════
        // 5. CHOP KILL SWITCH (v4.2: Imbalance can override displacement)
        // Crypto: flat price + strong imbalance = valid edge
        // v4.2.4: Bootstrap handled upstream via BootstrapEvaluator
        // ═══════════════════════════════════════════════════════════════════
        double chop_floor = std::max(spread_bps * 1.2, 1.0);
        if (displacement_bps < chop_floor && std::abs(orderbook_imbalance) < 0.15) {
            record_block(BlockReason::CHOP);
            return false;  // Both weak = chop
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: WIN RATE GATE 3 - RANGING HARD KILL
        // Low displacement + weak imbalance = hard no-trade zone
        // v4.2.4: Bootstrap handled upstream via BootstrapEvaluator
        // Win rate gain: +5-10%
        // ═══════════════════════════════════════════════════════════════════
        if (displacement_bps < spread_bps * 2.5 && std::abs(orderbook_imbalance) < 0.20) {
            record_block(BlockReason::RANGING);
            return false;  // Ranging market
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.2.2: WIN RATE GATE 4 - DIRECTIONAL BIAS FILTER (DAMPENED)
        // Only block if micro-trend is STRONG (|ema| > 0.8)
        // Allows mean-reversion scalps in normal conditions
        // ═══════════════════════════════════════════════════════════════════
        if (intended_direction != 0 && std::abs(micro_trend_ema_) > 0.8) {
            double micro_dir = micro_trend_ema_ > 0 ? 1.0 : -1.0;
            if ((micro_dir > 0 && intended_direction < 0) ||
                (micro_dir < 0 && intended_direction > 0)) {
                record_block(BlockReason::COUNTER_TREND);
                return false;  // Counter-trend blocked (strong trend only)
            }
        }
        
        // 6. COOLDOWN CHECK (after loss)
        if (now_ns < cooldown_until_ns_) {
            record_block(BlockReason::COOLDOWN);
            return false;
        }
        
        // 7. TRADE FREQUENCY LIMIT
        if (now_ns - trade_window_start_ > TRADE_WINDOW_NS) {
            // Reset window
            trade_window_start_ = now_ns;
            trades_in_window_ = 0;
        }
        if (trades_in_window_ >= MAX_TRADES_PER_WINDOW) {
            record_block(BlockReason::FREQUENCY);
            return false;
        }
        
        // ═══════════════════════════════════════════════════════════════════
        // POST-GATE: Vol cap only affects sizing/TP, NOT existence
        // (Sizing logic is handled by caller using final_edge)
        // ═══════════════════════════════════════════════════════════════════
        
        // Reset burst and edge confirmation on success (for next trade)
        edge_confirm_start_ns_ = 0;
        burst_start_ns_ = 0;
        
        return true;
    }
    
    // Update displacement tracking (call every tick)
    void update_displacement_tracking(double mid, uint64_t now_ns) noexcept {
        // Reset window if expired
        if (now_ns - displacement_window_start_ > DISPLACEMENT_WINDOW_NS) {
            displacement_window_start_ = now_ns;
            price_min_window_ = mid;
            price_max_window_ = mid;
        } else {
            price_min_window_ = std::min(price_min_window_, mid);
            price_max_window_ = std::max(price_max_window_, mid);
        }
    }
    
    // Get current displacement in bps
    [[nodiscard]] double get_displacement_bps() const noexcept {
        if (price_max_window_ <= 0 || price_min_window_ >= 1e17) return 0.0;
        double mid = (price_max_window_ + price_min_window_) / 2.0;
        if (mid <= 0) return 0.0;
        return (price_max_window_ - price_min_window_) / mid * 10000.0;
    }
    
    // Update realized volatility (short-term stddev)
    void update_realized_vol(double mid) noexcept {
        vol_sample_count_++;
        price_sum_ += mid;
        price_sum_sq_ += mid * mid;
        
        // Calculate stddev after enough samples (50+)
        if (vol_sample_count_ >= 50) {
            double mean = price_sum_ / vol_sample_count_;
            double variance = (price_sum_sq_ / vol_sample_count_) - (mean * mean);
            if (variance > 0 && mean > 0) {
                double stddev = std::sqrt(variance);
                realized_vol_bps_ = (stddev / mean) * 10000.0;
            }
            
            // Rolling window - decay old samples
            if (vol_sample_count_ >= 200) {
                price_sum_ *= 0.5;
                price_sum_sq_ *= 0.5;
                vol_sample_count_ = 100;
            }
        }
    }
    
    // Compute projected edge from current signals (RAW - no cap)
    // v4.2.2: CRITICAL FIX - Returns RAW edge for gating. Scaling done AFTER gate.
    [[nodiscard]] double compute_projected_edge(double imbalance, double momentum_bps) const noexcept {
        // Microprice deviation component
        double micro_component = std::abs(imbalance) * 10.0;  // Scale imbalance to bps
        
        // Momentum component (already in bps-like units)
        double mom_component = std::abs(momentum_bps);
        
        // RAW edge estimate - NO VOL CAP HERE (cap applied post-gate for sizing)
        return micro_component + mom_component;
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
    
    // v7.11: Testnet mode
    bool is_testnet_ = false;
    double last_mid_ = 0.0;
    double momentum_ema_ = 0.0;
    
    // v7.13: LIVE HFT - imbalance persistence tracking
    int last_imbalance_sign_ = 0;       // -1, 0, +1
    uint64_t imbalance_persist_ms_ = 0; // How long imbalance held direction
    
    // v7.14: REGIME + EXPECTANCY STATE (THE CORE SAFETY SYSTEM)
    // v4.3.2: Default to TRANSITION (not STABLE) to prevent trading during warmup
    Chimera::Crypto::MarketRegime current_regime_ = Chimera::Crypto::MarketRegime::TRANSITION;
    double current_expectancy_bps_ = 0.3;   // Start with small positive assumption (legacy compat)
    int expectancy_trades_ = 0;              // Number of trades used for expectancy calc
    double spread_ewma_ = 2.0;               // For regime classification
    double spread_std_ = 0.5;                // Spread volatility
    double book_flip_rate_ = 0.0;            // Top-of-book churn rate
    double last_best_bid_ = 0.0;             // v3.11: Per-thread last bid (was static!)
    double last_best_ask_ = 0.0;             // v3.11: Per-thread last ask (was static!)
    uint64_t last_regime_check_ts_ = 0;      // Rate limit regime classification
    
    // v7.14: DUAL-HORIZON AUTHORITY (fast protects, slow decides)
    Chimera::Risk::ExpectancyAuthority expectancy_authority_;
    
    // v7.14: HYSTERESIS GATES (prevent oscillation)
    Chimera::Control::HysteresisGate regime_hysteresis_{true, 0, 2000};  // 2s min hold
    Chimera::Control::CountHysteresis toxic_hysteresis_{2};  // 2 consecutive toxic signals
    
    // v3.0: SHADOW TRADING STATE (bootstraps expectancy without real orders)
    bool shadow_position_open_ = false;
    double shadow_entry_price_ = 0.0;
    uint64_t shadow_entry_ts_ = 0;
    int shadow_side_ = 0;  // +1 long, -1 short
    uint64_t shadow_trades_total_ = 0;
    uint64_t shadow_wins_ = 0;    // v4.0: Track wins
    uint64_t shadow_losses_ = 0;  // v4.0: Track losses
    
    // v4.2.2: Shadow trade callback for GUI/logging
    ShadowTradeCallback shadow_trade_callback_;
    
    // v4.2: CRITICAL HFT GATE - Missing invariants that killed win rate
    // Tracks price displacement (chop detection)
    double price_min_window_ = 1e18;     // Min price in window
    double price_max_window_ = 0.0;      // Max price in window
    uint64_t displacement_window_start_ = 0;
    static constexpr uint64_t DISPLACEMENT_WINDOW_NS = 500'000'000;  // 500ms
    
    // Volatility tracking (short-term realized vol)
    double price_sum_ = 0.0;
    double price_sum_sq_ = 0.0;
    uint64_t vol_sample_count_ = 0;
    double realized_vol_bps_ = 2.0;  // Start conservative
    
    // Cooldown after loss
    uint64_t cooldown_until_ns_ = 0;
    static constexpr uint64_t LOSS_COOLDOWN_NS = 300'000'000;  // 300ms after loss
    
    // Trade frequency limit
    uint64_t trades_in_window_ = 0;
    uint64_t trade_window_start_ = 0;
    static constexpr uint64_t TRADE_WINDOW_NS = 2'000'000'000;  // 2s window
    static constexpr uint64_t MAX_TRADES_PER_WINDOW = 1;  // 1 trade per 2s max
    
    // ═══════════════════════════════════════════════════════════════════════════
    // v4.2.2: WIN RATE IMPROVEMENTS - BURST-RELATIVE CONFIRMATION
    // ═══════════════════════════════════════════════════════════════════════════
    
    // 1. Edge confirmation (burst-relative)
    uint64_t edge_confirm_start_ns_ = 0;
    uint64_t burst_start_ns_ = 0;  // When current burst began
    uint64_t edge_confirm_ns_ = 12'000'000;  // Per-symbol, default 12ms for BTC/ETH
    
    // Confirmation bounds for crypto
    static constexpr uint64_t CRYPTO_MIN_CONFIRM_NS = 5'000'000;   // 5ms
    static constexpr uint64_t CRYPTO_MAX_CONFIRM_NS = 30'000'000;  // 30ms
    
    // 2. Micro-trend (1s rolling direction)
    double micro_trend_ema_ = 0.0;
    
    // 3. Rolling win rate for self-healing
    uint64_t trades_today_ = 0;
    uint64_t wins_today_ = 0;
    bool disabled_for_day_ = false;
    
    // 4. Block reason tracking
    enum class BlockReason : uint8_t {
        NONE = 0,
        EDGE_CONFIRMING,
        NO_BURST,
        COST_TOO_HIGH,
        EDGE_TOO_LOW,
        COUNTER_TREND,
        CHOP,
        COOLDOWN,
        DISPLACEMENT_LOW,
        SPREAD_TOO_WIDE,
        RANGING,
        FREQUENCY,
        BOOK_NOT_READY  // v4.2.2: Book not populated (spread=0)
    };
    
    BlockReason last_block_reason_ = BlockReason::NONE;
    std::array<uint64_t, 13> block_counts_ = {};  // v4.2.2: Increased from 12 to 13
    
    void record_block(BlockReason reason) {
        last_block_reason_ = reason;
        block_counts_[static_cast<size_t>(reason)]++;
    }
    
    static const char* block_reason_str(BlockReason r) {
        switch (r) {
            case BlockReason::NONE: return "NONE";
            case BlockReason::EDGE_CONFIRMING: return "EDGE_CONFIRMING";
            case BlockReason::NO_BURST: return "NO_BURST";
            case BlockReason::COST_TOO_HIGH: return "COST_TOO_HIGH";
            case BlockReason::EDGE_TOO_LOW: return "EDGE_TOO_LOW";
            case BlockReason::COUNTER_TREND: return "COUNTER_TREND";
            case BlockReason::CHOP: return "CHOP";
            case BlockReason::COOLDOWN: return "COOLDOWN";
            case BlockReason::DISPLACEMENT_LOW: return "DISPLACEMENT_LOW";
            case BlockReason::SPREAD_TOO_WIDE: return "SPREAD_TOO_WIDE";
            case BlockReason::RANGING: return "RANGING";
            case BlockReason::FREQUENCY: return "FREQUENCY";
            case BlockReason::BOOK_NOT_READY: return "BOOK_NOT_READY";
            default: return "UNKNOWN";
        }
    }
    
    BinanceCentralMicro micro_engine_;
    SignalAggregator signal_agg_;
    RegimeClassifier regime_classifier_;
    MultiStrategyCoordinator coordinator_;
    ExecutionGate exec_gate_;
    Bootstrap::BootstrapEvaluator bootstrap_;  // v4.2.4: Information-based bootstrap
    
    uint64_t tick_count_ = 0;
    uint64_t trade_count_ = 0;
    uint64_t last_tick_ts_ = 0;
    uint64_t orders_generated_ = 0;
    uint64_t non_flat_count_ = 0;
};

} // namespace Binance
} // namespace Chimera
