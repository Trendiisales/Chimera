#include "SymbolLane_ANTIPARALYSIS.hpp"
#include <iostream>
#include <cmath>

SymbolLane::SymbolLane(const std::string& sym)
    : symbol_(sym),
      net_bps_(0.0),
      dd_bps_(0.0),
      trade_count_(0),
      fees_(0.0),
      alloc_(1.0),
      leverage_(1.0),
      last_price_(0.0),
      position_(0.0),
      last_mid_(0.0),
      ticks_since_trade_(0),
      warmup_ticks_(0) {}

void SymbolLane::onTick(const chimera::MarketTick& tick) {
    if (tick.bid <= 0 || tick.ask <= 0) return;
    
    last_price_ = tick.last;
    double mid = (tick.bid + tick.ask) / 2.0;
    double spread_bps = ((tick.ask - tick.bid) / mid) * 10000.0;
    
    // WARMUP: Skip first 100 ticks to establish baseline
    if (warmup_ticks_ < 100) {
        warmup_ticks_++;
        last_mid_ = mid;  // Update baseline
        this->tick();
        return;
    }
    
    ticks_since_trade_++;
    
    // THROTTLE: Only check every 50 ticks
    if (ticks_since_trade_ < 50) {
        this->tick();
        return;
    }
    
    // GUARD: Ensure we have valid baseline
    if (last_mid_ <= 0) {
        last_mid_ = mid;
        this->tick();
        return;
    }
    
    double price_change_bps = ((mid - last_mid_) / last_mid_) * 10000.0;
    
    // SANITY CHECK: Reject absurd moves (>100 bps in 50 ticks = ~5 seconds)
    if (std::abs(price_change_bps) > 100.0) {
        last_mid_ = mid;  // Reset baseline
        this->tick();
        return;
    }
    
    // Trade on 5+ bps moves with tight spreads
    if (std::abs(price_change_bps) > 5.0 && spread_bps < 10.0) {
        ticks_since_trade_ = 0;
        trade_count_++;
        
        // Realistic edge: 30% of move minus fees
        double edge = price_change_bps * 0.3;
        double trade_pnl = edge - 0.8;
        
        net_bps_ += trade_pnl;
        fees_ += 0.0008 * leverage_;
        
        if (net_bps_ < dd_bps_) {
            dd_bps_ = net_bps_;
        }
        
        TelemetryTradeRow trade;
        trade.engine = symbol_;
        trade.symbol = tick.symbol;
        trade.side = (price_change_bps > 0) ? "BUY" : "SELL";
        trade.bps = trade_pnl;
        trade.latency_ms = 3 + (rand() % 5);
        trade.leverage = leverage_;
        
        TelemetryBus::instance().recordTrade(trade);
        
        if (trade_count_ % 10 == 0) {
            std::cout << "[TRADE] " << symbol_ 
                      << " #" << trade_count_
                      << " Move=" << price_change_bps << "bps"
                      << " PnL=" << trade_pnl << "bps"
                      << " Net=" << net_bps_ << "bps" << std::endl;
        }
    }
    
    last_mid_ = mid;
    this->tick();
}

void SymbolLane::tick() {
    TelemetryEngineRow row;
    row.symbol = symbol_;
    row.net_bps = net_bps_;
    row.dd_bps = dd_bps_;
    row.trades = trade_count_;
    row.fees = fees_;
    row.alloc = alloc_;
    row.leverage = leverage_;
    row.state = "LIVE";
    TelemetryBus::instance().updateEngine(row);
}
