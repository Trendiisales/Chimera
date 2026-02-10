#include "shadow/SymbolExecutor.hpp"
#include "risk/EdgeGate.hpp"
#include "risk/DrawdownGate.hpp"
#include <cmath>
#include <iostream>
#include <chrono>

namespace shadow {

SymbolExecutor::SymbolExecutor(const SymbolConfig& cfg, ExecMode mode, ExecutionRouter& router)
    : cfg_(cfg)
    , mode_(mode)
    , ledger_()
    , governor_(ledger_)
    , session_guard_({86400, 0, 0})
    , metal_type_(cfg.symbol == "XAUUSD" ? Metal::XAU : Metal::XAG)
    , router_(router)
    , realized_pnl_(0.0)
    , last_entry_ts_(0)
    , trades_this_hour_(0)
    , hour_start_ts_(0)
    , last_bid_(0.0)
    , last_ask_(0.0)
    , last_latency_ms_(10.0)
    , account_equity_(100000.0)
{
}

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
    
    // Check stops
    for (size_t i = 0; i < legs_.size(); ) {
        auto& leg = legs_[i];
        bool hit_stop = (leg.side == Side::BUY && t.bid <= leg.stop) ||
                        (leg.side == Side::SELL && t.ask >= leg.stop);
        
        if (hit_stop) {
            double exit_price = (leg.side == Side::BUY) ? t.bid : t.ask;
            double pnl = (leg.side == Side::BUY) ?
                         (exit_price - leg.entry) * leg.size :
                         (leg.entry - exit_price) * leg.size;
            
            uint64_t trade_id = leg_to_trade_[i];
            realized_pnl_ += pnl;
            
            std::cout << "[" << cfg_.symbol << "] EXIT SL trade_id=" 
                      << trade_id << " pnl=$" << pnl << "\n";
            
            if (exit_callback_) {
                exit_callback_(cfg_.symbol.c_str(), trade_id, exit_price, pnl, "SL");
            }
            
            // GUI callback for exit
            if (gui_callback_) {
                char side = (leg.side == Side::BUY) ? 'B' : 'S';
                gui_callback_(cfg_.symbol.c_str(), trade_id, side, leg.entry, exit_price, leg.size, pnl, t.ts_ms);
            }
            
            legs_.erase(legs_.begin() + i);
            leg_to_trade_.erase(i);
        } else {
            i++;
        }
    }
    
    // Check TP
    double tp_target = (metal_type_ == Metal::XAU) ? 2.0 : 0.02;
    
    for (size_t i = 0; i < legs_.size(); ) {
        auto& leg = legs_[i];
        double current_price = (leg.side == Side::BUY) ? t.bid : t.ask;
        double pnl = (leg.side == Side::BUY) ?
                     (current_price - leg.entry) * leg.size :
                     (leg.entry - current_price) * leg.size;
        
        if (pnl >= tp_target) {
            uint64_t trade_id = leg_to_trade_[i];
            realized_pnl_ += pnl;
            
            std::cout << "[" << cfg_.symbol << "] EXIT TP trade_id=" 
                      << trade_id << " pnl=$" << pnl << "\n";
            
            if (exit_callback_) {
                exit_callback_(cfg_.symbol.c_str(), trade_id, current_price, pnl, "TP");
            }
            
            // GUI callback for exit
            if (gui_callback_) {
                char side = (leg.side == Side::BUY) ? 'B' : 'S';
                gui_callback_(cfg_.symbol.c_str(), trade_id, side, leg.entry, current_price, leg.size, pnl, t.ts_ms);
            }
            
            legs_.erase(legs_.begin() + i);
            leg_to_trade_.erase(i);
        } else {
            i++;
        }
    }
}

void SymbolExecutor::onSignal(const Signal& s, uint64_t ts_ms) {
    if (!canEnter(s, ts_ms)) {
        return;
    }
    
    double price = (s.side == Side::BUY) ? last_ask_ : last_bid_;
    enterBase(s.side, price, ts_ms);
}

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

void SymbolExecutor::enterBase(Side side, double price, uint64_t ts) {
    double size = cfg_.base_size;
    
    TradeSide tside = (side == Side::BUY) ? TradeSide::BUY : TradeSide::SELL;
    uint64_t trade_id = governor_.commit_entry(
        cfg_.symbol, tside, size, price, last_bid_, last_ask_, last_latency_ms_, ts
    );
    
    Leg leg;
    leg.side = side;
    leg.size = size;
    leg.entry = price;
    leg.entry_ts = ts;
    leg.stop = (side == Side::BUY) ? price - cfg_.initial_stop : price + cfg_.initial_stop;
    
    legs_.push_back(leg);
    leg_to_trade_[legs_.size()-1] = trade_id;
    
    last_entry_ts_ = ts;
    trades_this_hour_++;
    
    std::cout << "[" << cfg_.symbol << "] ENTRY trade_id=" << trade_id
              << " price=" << price << " size=" << size
              << " (" << trades_this_hour_ << "/60)\n";
    
    // GUI callback for entry
    if (gui_callback_) {
        char side_char = (side == Side::BUY) ? 'B' : 'S';
        gui_callback_(cfg_.symbol.c_str(), trade_id, side_char, price, 0.0, size, 0.0, ts);
    }
}

void SymbolExecutor::exitAll(const char* reason, double price, uint64_t ts) {
    for (size_t i = 0; i < legs_.size(); i++) {
        auto& leg = legs_[i];
        uint64_t trade_id = leg_to_trade_[i];
        
        double pnl = (leg.side == Side::BUY) ?
                     (price - leg.entry) * leg.size :
                     (leg.entry - price) * leg.size;
        
        realized_pnl_ += pnl;
        governor_.commit_exit(trade_id, price, ts);
        
        std::cout << "[" << cfg_.symbol << "] EXIT " << reason
                  << " trade_id=" << trade_id << " pnl=$" << pnl << "\n";
        
        if (exit_callback_) {
            exit_callback_(cfg_.symbol.c_str(), trade_id, price, pnl, reason);
        }
        
        if (gui_callback_) {
            char side = (leg.side == Side::BUY) ? 'B' : 'S';
            gui_callback_(cfg_.symbol.c_str(), trade_id, side, leg.entry, price, leg.size, pnl, ts);
        }
    }
    
    legs_.clear();
    leg_to_trade_.clear();
}

double SymbolExecutor::getRealizedPnL() const {
    return realized_pnl_;
}

void SymbolExecutor::setGUICallback(GUITradeCallback cb) {
    gui_callback_ = std::move(cb);
}

void SymbolExecutor::setExitCallback(ExitCallback cb) {
    exit_callback_ = std::move(cb);
}

int SymbolExecutor::getActiveLegs() const {
    return static_cast<int>(legs_.size());
}

void SymbolExecutor::status() const {
    std::cout << "[" << cfg_.symbol << "] legs=" << legs_.size()
              << " pnl=$" << realized_pnl_
              << " trades=" << trades_this_hour_
              << " rejects=" << rejection_stats_.total_rejections
              << " latency_rejects=" << rejection_stats_.latency_rejects << "\n";
}

} // namespace shadow
