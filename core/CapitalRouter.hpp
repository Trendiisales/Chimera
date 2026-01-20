#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <cstdint>

// OrderIntent structure for capital requests
struct OrderIntent {
    double notional_usd = 0.0;
    double confidence = 1.0;
    bool buy = true;
};

/**
 * CapitalRouter - thread-safe capital allocation across symbol lanes
 * 
 * Responsibilities:
 * - Allocate capital floors to each symbol
 * - Scale capital based on edge confidence
 * - Thread-safe for concurrent lane access
 * 
 * Shared resource - accessed by all lanes
 * Protected by internal mutex
 */
class CapitalRouter {
public:
    explicit CapitalRouter(double total_capital)
        : total_capital_(total_capital) {}

    // Set capital floor for a symbol (% of total)
    void setFloor(const std::string& symbol, double pct) {
        std::lock_guard<std::mutex> lock(mtx_);
        floors_[symbol] = pct;
    }

    // Request capital for a trade
    // Returns: actual notional USD to use
    double request(const std::string& symbol, double edge_confidence) {
        std::lock_guard<std::mutex> lock(mtx_);

        auto it = floors_.find(symbol);
        if (it == floors_.end()) {
            return 0.0;
        }

        // Floor capital allocated to this symbol
        double floor_cap = total_capital_ * it->second;
        
        // Scale by edge confidence [0.0, 1.0]
        double scaled = floor_cap * std::clamp(edge_confidence, 0.0, 1.0);

        return scaled;
    }

    // Price for maker orders - cross spread minimally
    double priceFor(const OrderIntent& intent, double current_price) const {
        if (current_price <= 0.0) {
            // Log warning but don't spam
            static uint64_t warn_count = 0;
            if (++warn_count % 1000 == 0) {
                std::cerr << "[ROUTER] WARNING: Invalid price=" << current_price << std::endl;
            }
            return 0.0;
        }
        // Fade: place slightly inside the spread
        return intent.buy ? current_price * 0.9999 : current_price * 1.0001;
    }

    // Quantity from notional with Binance minimum enforcement
    double qtyFor(const OrderIntent& intent, double current_price) const {
        if (current_price <= 0.0) {
            static uint64_t warn_count = 0;
            if (++warn_count % 1000 == 0) {
                std::cerr << "[ROUTER] WARNING: Invalid price=" << current_price << " for qty calc" << std::endl;
            }
            return 0.0;
        }
        
        double usd = intent.notional_usd * intent.confidence;
        double raw_qty = usd / current_price;
        
        // VALIDATION MODE: Temporarily lowered for first trades
        // Raise back to Binance minimums after fills confirmed
        const double MIN_NOTIONAL = 2.0;  // Temporarily $2 (normally $5)
        const double MIN_QTY = 0.00001;   // Temporarily 0.00001 (normally 0.0001)
        
        // Check if notional is too small
        if (usd < MIN_NOTIONAL) {
            static uint64_t warn_count = 0;
            if (++warn_count % 100 == 0) {  // Log more frequently
                std::cerr << "[ROUTER] BLOCK: Notional $" << usd << " < $" << MIN_NOTIONAL 
                         << " (confidence=" << intent.confidence << ")" << std::endl;
            }
            return 0.0;
        }
        
        // Check if quantity is too small
        if (raw_qty < MIN_QTY) {
            static uint64_t warn_count = 0;
            if (++warn_count % 100 == 0) {  // Log more frequently
                std::cerr << "[ROUTER] BLOCK: Qty " << raw_qty << " < min " << MIN_QTY
                         << " (notional=$" << usd << " price=$" << current_price << ")" << std::endl;
            }
            return 0.0;
        }
        
        // Verify final notional after rounding
        double final_notional = raw_qty * current_price;
        if (final_notional < MIN_NOTIONAL) {
            static uint64_t warn_count = 0;
            if (++warn_count % 100 == 0) {
                std::cerr << "[ROUTER] BLOCK: Final notional $" << final_notional << " < $" << MIN_NOTIONAL 
                         << " after qty rounding" << std::endl;
            }
            return 0.0;
        }
        
        return raw_qty;
    }

    // Scale by confidence
    double scale(const OrderIntent& intent) const {
        return intent.notional_usd * intent.confidence;
    }

    double route(const std::string& sym, double confidence, double px) {
        std::lock_guard<std::mutex> lock(mtx_);

        auto it = floors_.find(sym);
        if (it == floors_.end()) {
            return 0.0;
        }

        double floor_cap = total_capital_ * it->second;
        double scaled = floor_cap * std::clamp(confidence, 0.1, 1.0);

        const double MIN_VIABLE = 2.0;
        if (scaled < MIN_VIABLE) {
            scaled = MIN_VIABLE;
        }

        return scaled / px;
    }

private:
    std::mutex mtx_;
    double total_capital_;
    std::unordered_map<std::string, double> floors_;
};
