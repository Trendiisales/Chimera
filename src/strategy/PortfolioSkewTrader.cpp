#include "PortfolioSkewTrader.hpp"
#include "control/UnwindCoordinator.hpp"
#include <chrono>
#include <cmath>
#include <iostream>

namespace chimera {

extern UnwindCoordinator g_unwind_coordinator;

PortfolioSkewTrader::PortfolioSkewTrader()
    : engine_id_("PORTFOLIO_SKEW")
{}

const std::string& PortfolioSkewTrader::id() const {
    return engine_id_;
}

void PortfolioSkewTrader::onTick(const MarketTick& tick, std::vector<OrderIntent>& out) {
    // Update portfolio position for this symbol
    portfolio_pos_[tick.symbol] = tick.position;
    
    double pos = tick.position;
    double abs_pos = std::fabs(pos);
    
    // ---------------------------------------------------------------------------
    // FORCED UNWIND — if at position cap, unwind immediately.
    // This check happens FIRST, before throttling, before UnwindCoordinator.
    // When at cap, we MUST reduce position. No edge checks, no delays.
    // Atomic position gate in ExecutionRouter ensures we can't overshoot.
    // ---------------------------------------------------------------------------
    if (abs_pos >= MAX_POS_PER_SYMBOL) {
        using namespace std::chrono;
        uint64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        
        auto& st = state_[tick.symbol];
        
        // Even forced unwinds need minimal throttling to avoid spam
        // But use faster throttle (10ms instead of 100ms)
        static constexpr uint64_t UNWIND_THROTTLE_NS = 10'000'000ULL;  // 10ms
        
        if (now - st.last_submit_ns < UNWIND_THROTTLE_NS) {
            return;  // Wait briefly before next unwind attempt
        }
        
        OrderIntent o;
        o.engine_id = engine_id_;
        o.symbol = tick.symbol;
        o.is_buy = (pos < 0);  // If short, buy to unwind; if long, sell to unwind
        o.price = o.is_buy ? tick.ask : tick.bid;  // Aggressive: cross spread
        o.size = BASE_QTY;
        out.push_back(o);
        st.last_submit_ns = now;
        
        std::cout << "[PORTFOLIO_SKEW] UNWIND_FORCED " << tick.symbol 
                  << " pos=" << pos << " size=" << o.size 
                  << (o.is_buy ? " BUY" : " SELL") << " @ " << o.price << "\n";
        return;  // Skip normal trading when unwinding
    }
    
    // ---------------------------------------------------------------------------
    // Normal trading mode — position is within limits
    // ---------------------------------------------------------------------------
    
    // UnwindCoordinator prevents fighting at position caps
    // This is secondary protection - primary is the check above
    g_unwind_coordinator.try_lock(tick.symbol, engine_id_, pos);
    if (!g_unwind_coordinator.can_trade(tick.symbol, engine_id_)) {
        return;  // Another engine is unwinding, stay out of the way
    }
    g_unwind_coordinator.check_release(tick.symbol, pos);

    using namespace std::chrono;
    uint64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    
    // Per-symbol throttling state
    auto& st = state_[tick.symbol];
    
    // Normal throttling for non-forced trades
    if (now - st.last_submit_ns < THROTTLE_NS) return;

    double bid = tick.bid;
    double ask = tick.ask;
    double mid = (bid + ask) / 2.0;
    double spread_bps = ((ask - bid) / mid) * 10000.0;
    
    // Only trade on reasonable spreads
    if (spread_bps > 15.0) return;

    // Calculate portfolio-level metrics
    double total_abs_pos = 0.0;
    double total_signed_pos = 0.0;
    int num_symbols = 0;
    
    for (const auto& [sym, p] : portfolio_pos_) {
        total_abs_pos += std::fabs(p);
        total_signed_pos += p;
        num_symbols++;
    }
    
    if (num_symbols == 0) return;
    
    // Portfolio imbalance: if we're net long or short across all symbols
    double portfolio_imbalance = total_signed_pos / (num_symbols > 0 ? num_symbols : 1);
    
    // Symbol-specific skew: how much this symbol deviates from zero
    double symbol_skew = pos;
    
    // Decide if we need to rebalance this symbol
    bool should_reduce_long = false;
    bool should_reduce_short = false;
    
    // If this symbol is significantly skewed relative to threshold
    if (std::fabs(symbol_skew) > SKEW_THRESHOLD) {
        should_reduce_long = (symbol_skew > 0);
        should_reduce_short = (symbol_skew < 0);
    }
    
    // Or if portfolio as a whole is imbalanced and this symbol contributes to it
    if (std::fabs(portfolio_imbalance) > SKEW_THRESHOLD * PORTFOLIO_K) {
        if (portfolio_imbalance > 0 && pos > 0) {
            should_reduce_long = true;  // Portfolio too long, reduce long positions
        }
        else if (portfolio_imbalance < 0 && pos < 0) {
            should_reduce_short = true;  // Portfolio too short, reduce short positions
        }
    }
    
    // Book imbalance signal for timing
    double book_imbalance = (tick.bid_size - tick.ask_size) / (tick.bid_size + tick.ask_size + 1e-6);
    
    if (should_reduce_long) {
        // We're too long - look to sell when book is favorable
        double edge_signal = -symbol_skew * 10.0 + book_imbalance * 5.0;
        
        if (edge_signal > EDGE_BPS) {
            OrderIntent o;
            o.engine_id = engine_id_;
            o.symbol = tick.symbol;
            o.is_buy = false;
            o.price = bid;
            o.size = BASE_QTY;
            out.push_back(o);
            st.last_submit_ns = now;
            std::cout << "[PORTFOLIO_SKEW] REDUCE_LONG " << tick.symbol 
                      << " pos=" << pos << " port_imb=" << portfolio_imbalance << "\n";
        }
    }
    else if (should_reduce_short) {
        // We're too short - look to buy when book is favorable
        double edge_signal = symbol_skew * 10.0 - book_imbalance * 5.0;
        
        if (edge_signal > EDGE_BPS) {
            OrderIntent o;
            o.engine_id = engine_id_;
            o.symbol = tick.symbol;
            o.is_buy = true;
            o.price = ask;
            o.size = BASE_QTY;
            out.push_back(o);
            st.last_submit_ns = now;
            std::cout << "[PORTFOLIO_SKEW] REDUCE_SHORT " << tick.symbol 
                      << " pos=" << pos << " port_imb=" << portfolio_imbalance << "\n";
        }
    }
}

}
