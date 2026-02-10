bool SymbolExecutor::canEnter(const Signal& s, uint64_t ts_ms) {
    // Max legs check
    if (legs_.size() >= static_cast<size_t>(cfg_.max_legs)) {
        std::cout << "[" << cfg_.symbol << "] REJECT: MAX_LEGS (legs=" 
                  << legs_.size() << ")\n";
        rejection_stats_.total_rejections++;
        return false;
    }
    
    // Hour limit
    uint32_t hour_limit = (metal_type_ == Metal::XAU) ? 60 : 30;
    if (trades_this_hour_ >= hour_limit) {
        rejection_stats_.total_rejections++;
        return false;
    }
    
    // Get current impulse
    double velocity = router_.get_velocity(cfg_.symbol);
    double abs_impulse = std::abs(velocity);
    
    // Profit governor entry gate (includes cooldown and freeze logic)
    uint64_t now_ns = ts_ms * 1'000'000;
    if (!profit_governor_.allow_entry(abs_impulse, now_ns)) {
        std::cout << "[" << cfg_.symbol << "] REJECT: ENTRY_FREEZE/COOLDOWN"
                  << " (impulse=" << abs_impulse << ")\n";
        rejection_stats_.total_rejections++;
        return false;
    }
    
    // ExecutionRouter gate (latency + velocity + stall check)
    std::string reject_reason;
    bool is_buy = (s.side == Side::BUY);
    
    if (!router_.submit_signal(cfg_.symbol, is_buy, ts_ms, reject_reason)) {
        rejection_stats_.latency_rejects++;
        rejection_stats_.total_rejections++;
        return false;
    }
    
    std::cout << "[" << cfg_.symbol << "] ✓ ENTRY ALLOWED\n";
    return true;
}
