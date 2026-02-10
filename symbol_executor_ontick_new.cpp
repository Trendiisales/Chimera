void SymbolExecutor::onTick(const Tick& t) {
    last_bid_ = t.bid;
    last_ask_ = t.ask;
    
    // Feed quote to ExecutionRouter for velocity calculation
    Quote q;
    q.bid = t.bid;
    q.ask = t.ask;
    q.ts_ms = t.ts_ms;
    router_.on_quote(cfg_.symbol, q);
    
    uint64_t current_hour = t.ts_ms / 3600000;
    if (current_hour != hour_start_ts_ / 3600000) {
        trades_this_hour_ = 0;
        hour_start_ts_ = t.ts_ms;
    }
    
    uint64_t now_ns = t.ts_ms * 1'000'000;  // Convert ms to ns
    
    // Update profit governor and check stops
    for (size_t i = 0; i < legs_.size(); ) {
        auto& leg = legs_[i];
        bool is_long = (leg.side == Side::BUY);
        double current_price = is_long ? t.bid : t.ask;
        
        // Calculate favorable and adverse moves
        double price_move = is_long ?
                           (current_price - leg.entry) :
                           (leg.entry - current_price);
        
        double favorable_move = (price_move > 0) ? price_move : 0.0;
        double adverse_move = (price_move < 0) ? -price_move : 0.0;
        
        // Update profit governor stop logic
        profit_governor_.maybe_enable_trailing(favorable_move);
        profit_governor_.update_stop(current_price, adverse_move, is_long);
        
        // Use profit governor stop price
        leg.stop = profit_governor_.stop_price;
        
        // Check if stop hit
        bool hit_stop = (is_long && t.bid <= leg.stop) ||
                       (!is_long && t.ask >= leg.stop);
        
        if (hit_stop) {
            double exit_price = is_long ? t.bid : t.ask;
            double pnl = (is_long) ?
                         (exit_price - leg.entry) * leg.size :
                         (leg.entry - exit_price) * leg.size;
            
            uint64_t trade_id = leg_to_trade_[i];
            realized_pnl_ += pnl;
            
            std::cout << "[" << cfg_.symbol << "] EXIT SL trade_id=" 
                      << trade_id << " pnl=$" << pnl 
                      << " (stop=" << leg.stop << ")\n";
            
            if (exit_callback_) {
                exit_callback_(cfg_.symbol.c_str(), trade_id, exit_price, pnl, "SL");
            }
            
            if (gui_callback_) {
                char side = is_long ? 'B' : 'S';
                gui_callback_(cfg_.symbol.c_str(), trade_id, side, leg.entry, exit_price, leg.size, pnl, t.ts_ms);
            }
            
            // Notify profit governor of exit
            profit_governor_.on_exit(now_ns);
            
            legs_.erase(legs_.begin() + i);
            leg_to_trade_.erase(i);
            continue;
        }
        
        i++;
    }
    
    // Check TP (price-based with latency multiplier)
    for (size_t i = 0; i < legs_.size(); ) {
        auto& leg = legs_[i];
        bool hit_tp = (leg.side == Side::BUY && t.bid >= leg.take_profit) ||
                      (leg.side == Side::SELL && t.ask <= leg.take_profit);
        
        if (hit_tp) {
            double exit_price = (leg.side == Side::BUY) ? t.bid : t.ask;
            double pnl = (leg.side == Side::BUY) ?
                         (exit_price - leg.entry) * leg.size :
                         (leg.entry - exit_price) * leg.size;
            
            uint64_t trade_id = leg_to_trade_[i];
            realized_pnl_ += pnl;
            
            std::cout << "[" << cfg_.symbol << "] EXIT TP trade_id=" 
                      << trade_id << " pnl=$" << pnl << "\n";
            
            if (exit_callback_) {
                exit_callback_(cfg_.symbol.c_str(), trade_id, exit_price, pnl, "TP");
            }
            
            if (gui_callback_) {
                char side = (leg.side == Side::BUY) ? 'B' : 'S';
                gui_callback_(cfg_.symbol.c_str(), trade_id, side, leg.entry, exit_price, leg.size, pnl, t.ts_ms);
            }
            
            // Notify profit governor of exit
            profit_governor_.on_exit(now_ns);
            
            legs_.erase(legs_.begin() + i);
            leg_to_trade_.erase(i);
        } else {
            i++;
        }
    }
}
