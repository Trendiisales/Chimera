// ═══════════════════════════════════════════════════════════════════════════════
// crypto_engine/include/binance/OrderBook.hpp
// v6.93: Fixed for @depth20 full snapshot updates
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include "BinanceParser.hpp"  // For PriceLevel

namespace Chimera {
namespace Binance {

// Simple order book for top N levels
class OrderBook {
public:
    static constexpr size_t MAX_LEVELS = 20;  // Increased for @depth20
    
    OrderBook() noexcept : symbol_id(0), last_update_id(0) {}
    
    void clear() noexcept {
        for (auto& b : bids_) { b.price = 0; b.quantity = 0; }
        for (auto& a : asks_) { a.price = 0; a.quantity = 0; }
        last_update_id = 0;
    }
    
    // For @depth20 stream: replace entire book with snapshot
    void set_full_depth(const PriceLevel* bids, uint8_t bid_count,
                        const PriceLevel* asks, uint8_t ask_count) noexcept {
        // Clear and replace bids
        for (size_t i = 0; i < MAX_LEVELS; ++i) {
            if (i < bid_count) {
                bids_[i] = bids[i];
            } else {
                bids_[i].price = 0;
                bids_[i].quantity = 0;
            }
        }
        
        // Clear and replace asks
        for (size_t i = 0; i < MAX_LEVELS; ++i) {
            if (i < ask_count) {
                asks_[i] = asks[i];
            } else {
                asks_[i].price = 0;
                asks_[i].quantity = 0;
            }
        }
        
        // Sort bids descending (highest first)
        std::sort(bids_.begin(), bids_.end(), [](const PriceLevel& a, const PriceLevel& b) {
            return a.price > b.price;
        });
        
        // Sort asks ascending (lowest first), 0s at end
        std::sort(asks_.begin(), asks_.end(), [](const PriceLevel& a, const PriceLevel& b) {
            if (a.price == 0) return false;
            if (b.price == 0) return true;
            return a.price < b.price;
        });
    }
    
    void update_bid(double price, double qty) noexcept {
        if (qty <= 0) {
            // Remove level
            for (auto& b : bids_) {
                if (b.price == price) {
                    b.price = 0;
                    b.quantity = 0;
                    break;
                }
            }
        } else {
            // Update or insert
            bool found = false;
            for (auto& b : bids_) {
                if (b.price == price) {
                    b.quantity = qty;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Find empty slot or lowest bid
                for (auto& b : bids_) {
                    if (b.price == 0 || price > b.price) {
                        b.price = price;
                        b.quantity = qty;
                        break;
                    }
                }
            }
        }
        // Sort bids descending
        std::sort(bids_.begin(), bids_.end(), [](const PriceLevel& a, const PriceLevel& b) {
            return a.price > b.price;
        });
    }
    
    void update_ask(double price, double qty) noexcept {
        if (qty <= 0) {
            // Remove level
            for (auto& a : asks_) {
                if (a.price == price) {
                    a.price = 0;
                    a.quantity = 0;
                    break;
                }
            }
        } else {
            // Update or insert
            bool found = false;
            for (auto& a : asks_) {
                if (a.price == price) {
                    a.quantity = qty;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Find empty slot or highest ask
                for (auto& a : asks_) {
                    if (a.price == 0 || price < a.price) {
                        a.price = price;
                        a.quantity = qty;
                        break;
                    }
                }
            }
        }
        // Sort asks ascending
        std::sort(asks_.begin(), asks_.end(), [](const PriceLevel& a, const PriceLevel& b) {
            if (a.price == 0) return false;
            if (b.price == 0) return true;
            return a.price < b.price;
        });
    }
    
    [[nodiscard]] double best_bid() const noexcept { 
        return bids_[0].price; 
    }
    
    [[nodiscard]] double best_ask() const noexcept { 
        return asks_[0].price; 
    }
    
    [[nodiscard]] double best_bid_qty() const noexcept { 
        return bids_[0].quantity; 
    }
    
    [[nodiscard]] double best_ask_qty() const noexcept { 
        return asks_[0].quantity; 
    }
    
    [[nodiscard]] double mid() const noexcept {
        return (bids_[0].price + asks_[0].price) / 2.0;
    }
    
    [[nodiscard]] double spread() const noexcept {
        return asks_[0].price - bids_[0].price;
    }
    
    [[nodiscard]] bool valid() const noexcept {
        return bids_[0].price > 0 && asks_[0].price > 0 && asks_[0].price > bids_[0].price;
    }
    
    // Book validity metrics for diagnostics
    [[nodiscard]] uint8_t bid_levels() const noexcept {
        uint8_t count = 0;
        for (const auto& b : bids_) {
            if (b.price > 0) ++count;
        }
        return count;
    }
    
    [[nodiscard]] uint8_t ask_levels() const noexcept {
        uint8_t count = 0;
        for (const auto& a : asks_) {
            if (a.price > 0) ++count;
        }
        return count;
    }
    
    [[nodiscard]] double spread_bps() const noexcept {
        if (!valid()) return 0.0;
        return (asks_[0].price - bids_[0].price) / mid() * 10000.0;
    }
    
    uint16_t symbol_id;
    uint64_t last_update_id;
    
private:
    std::array<PriceLevel, MAX_LEVELS> bids_{};
    std::array<PriceLevel, MAX_LEVELS> asks_{};
};

} // namespace Binance
} // namespace Chimera
