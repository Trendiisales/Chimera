void SymbolExecutor::enterBase(Side side, double price, uint64_t ts) {
    // Get current latency regime and velocity
    LatencyRegime regime = router_.latency().regime();
    double velocity = router_.get_velocity(cfg_.symbol);
    
    bool is_buy = (side == Side::BUY);
    
    // Calculate impulse-scaled size
    static ImpulseSizer impulse_sizer;
    SizeDecision size_decision = impulse_sizer.compute(
        cfg_.symbol, regime, velocity, is_buy
    );
    
    double size = cfg_.base_size * size_decision.multiplier;
    
    // Hard clamp for safety
    if (size > cfg_.base_size * 1.20) {
        size = cfg_.base_size * 1.20;
    }
    
    TradeSide tside = (side == Side::BUY) ? TradeSide::BUY : TradeSide::SELL;
    uint64_t trade_id = governor_.commit_entry(
        cfg_.symbol, tside, size, price, last_bid_, last_ask_, last_latency_ms_, ts
    );
    
    // Calculate latency-aware TP multiplier
    static LatencyAwareTP latency_tp;
    TPDecision tp_decision = latency_tp.compute(cfg_.symbol, regime, cfg_.initial_tp);
    double adjusted_tp = cfg_.initial_tp * tp_decision.tp_multiplier;
    
    // Initialize profit governor stop (TWO-PHASE LOGIC)
    profit_governor_.init_stop(price, is_buy);
    
    Leg leg;
    leg.side = side;
    leg.size = size;
    leg.entry = price;
    leg.entry_ts = ts;
    leg.entry_impulse = std::abs(velocity);
    leg.stop = profit_governor_.stop_price;  // Use profit governor's hard stop
    leg.take_profit = (side == Side::BUY) ? price + adjusted_tp : price - adjusted_tp;
    
    legs_.push_back(leg);
    leg_to_trade_[legs_.size()-1] = trade_id;
    
    last_entry_ts_ = ts;
    trades_this_hour_++;
    
    std::cout << "[" << cfg_.symbol << "] ENTRY trade_id=" << trade_id
              << " price=" << price 
              << " size=" << size << " (" << size_decision.multiplier << "x, " << size_decision.reason << ")"
              << " tp=" << adjusted_tp << " (" << tp_decision.tp_multiplier << "x, " << tp_decision.reason << ")"
              << " stop=" << leg.stop << " (HARD)"
              << " impulse=" << leg.entry_impulse
              << " (" << trades_this_hour_ << "/60)\n";
    
    // GUI callback for entry
    if (gui_callback_) {
        char side_char = (side == Side::BUY) ? 'B' : 'S';
        gui_callback_(cfg_.symbol.c_str(), trade_id, side_char, price, 0.0, size, 0.0, ts);
    }
}
