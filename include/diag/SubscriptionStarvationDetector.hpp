// =============================================================================
// SubscriptionStarvationDetector.hpp - Detect Silent FIX Subscription Failures
// =============================================================================
// PURPOSE: Alert when a symbol is subscribed but stops receiving quotes
// 
// ROOT CAUSE WE'RE SOLVING:
//   XAUUSD only received 2 quotes vs 408 for EURUSD
//   The subscription appeared to work but quotes weren't flowing
//   This silent failure meant the scalper could NEVER evaluate XAUUSD
//
// HOW IT WORKS:
//   1. Track every symbol that gets subscribed
//   2. Count quotes per symbol
//   3. Every N seconds, check for symbols with zero/low quotes
//   4. Log LOUD warnings for starved symbols
//   5. Optionally trigger re-subscription
//
// USAGE:
//   detector.onSubscribed("XAUUSD");
//   detector.onQuote("XAUUSD", bid, ask);  // Call on every quote
//   detector.periodicCheck(now_ms);         // Call every ~10s
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <algorithm>

namespace Chimera {

struct SymbolQuoteStats {
    uint64_t subscribed_at_ms = 0;
    uint64_t last_quote_ms    = 0;
    uint64_t quote_count      = 0;
    uint64_t quotes_since_check = 0;
    bool     warned           = false;
    bool     starved          = false;
};

class SubscriptionStarvationDetector {
public:
    using ResubscribeCallback = std::function<void(const std::string& symbol)>;

    explicit SubscriptionStarvationDetector(
        uint64_t starvation_threshold_ms = 30000,  // 30 seconds no quotes = starved
        uint64_t check_interval_ms = 10000,        // Check every 10 seconds
        uint64_t min_quotes_per_check = 1          // Expect at least 1 quote per check
    )
        : starvation_threshold_ms_(starvation_threshold_ms)
        , check_interval_ms_(check_interval_ms)
        , min_quotes_per_check_(min_quotes_per_check)
    {}

    // =========================================================================
    // LIFECYCLE EVENTS
    // =========================================================================
    
    void onSubscribed(const std::string& symbol, uint64_t now_ms) {
        auto& s = stats_[symbol];
        s.subscribed_at_ms = now_ms;
        s.last_quote_ms = 0;
        s.quote_count = 0;
        s.quotes_since_check = 0;
        s.warned = false;
        s.starved = false;
        
        printf("[STARVATION-DETECTOR] Tracking %s (subscribed at %lu ms)\n", 
               symbol.c_str(), now_ms);
    }

    void onUnsubscribed(const std::string& symbol) {
        stats_.erase(symbol);
        printf("[STARVATION-DETECTOR] Stopped tracking %s\n", symbol.c_str());
    }

    // =========================================================================
    // QUOTE HANDLER (call on EVERY quote)
    // =========================================================================
    
    void onQuote(const std::string& symbol, double bid, double ask, uint64_t now_ms) {
        auto it = stats_.find(symbol);
        if (it == stats_.end()) {
            // Quote for untracked symbol - auto-track it
            onSubscribed(symbol, now_ms);
            it = stats_.find(symbol);
        }
        
        auto& s = it->second;
        s.last_quote_ms = now_ms;
        s.quote_count++;
        s.quotes_since_check++;
        
        // If was starved, log recovery
        if (s.starved) {
            printf("[STARVATION-DETECTOR] *** %s RECOVERED *** (got quote after starvation)\n",
                   symbol.c_str());
            s.starved = false;
            s.warned = false;
        }
    }

    // =========================================================================
    // PERIODIC CHECK (call every ~10 seconds)
    // =========================================================================
    
    void periodicCheck(uint64_t now_ms) {
        if (now_ms - last_check_ms_ < check_interval_ms_) return;
        last_check_ms_ = now_ms;

        std::vector<std::string> starved_symbols;
        
        printf("\n[STARVATION-CHECK] === Symbol Quote Health @ %lu ms ===\n", now_ms);
        
        for (auto& kv : stats_) {
            const auto& sym = kv.first;
            auto& s = kv.second;
            
            uint64_t time_since_last = (s.last_quote_ms > 0) 
                ? (now_ms - s.last_quote_ms) 
                : (now_ms - s.subscribed_at_ms);
            
            // Calculate quote rate
            double elapsed_sec = (now_ms - s.subscribed_at_ms) / 1000.0;
            double quotes_per_sec = (elapsed_sec > 0) ? s.quote_count / elapsed_sec : 0;
            
            // Status indicators
            const char* status;
            if (s.quotes_since_check == 0 && time_since_last > starvation_threshold_ms_) {
                status = "🔴 STARVED";
                s.starved = true;
                starved_symbols.push_back(sym);
            } else if (s.quotes_since_check < min_quotes_per_check_) {
                status = "🟡 LOW";
            } else {
                status = "🟢 OK";
            }
            
            printf("[STARVATION-CHECK] %s: %s | total=%lu | since_check=%lu | last=%lu ms ago | rate=%.2f/s\n",
                   sym.c_str(),
                   status,
                   s.quote_count,
                   s.quotes_since_check,
                   time_since_last,
                   quotes_per_sec);
            
            // Reset counter for next check
            s.quotes_since_check = 0;
        }
        
        printf("[STARVATION-CHECK] ========================================\n\n");
        
        // Handle starved symbols
        for (const auto& sym : starved_symbols) {
            auto& s = stats_[sym];
            
            if (!s.warned) {
                printf("╔══════════════════════════════════════════════════════════════╗\n");
                printf("║  ⚠️  CRITICAL: %s IS STARVED - NO QUOTES IN %lu SEC    ║\n",
                       sym.c_str(), starvation_threshold_ms_ / 1000);
                printf("║  → Strategy CANNOT evaluate this symbol                       ║\n");
                printf("║  → Check FIX subscription / market data routing               ║\n");
                printf("╚══════════════════════════════════════════════════════════════╝\n");
                s.warned = true;
            }
            
            // Trigger re-subscription if callback set
            if (resubscribe_cb_) {
                printf("[STARVATION-DETECTOR] Triggering re-subscribe for %s\n", sym.c_str());
                resubscribe_cb_(sym);
            }
        }
    }

    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    
    void setResubscribeCallback(ResubscribeCallback cb) {
        resubscribe_cb_ = std::move(cb);
    }

    void setStarvationThreshold(uint64_t ms) { starvation_threshold_ms_ = ms; }
    void setCheckInterval(uint64_t ms) { check_interval_ms_ = ms; }
    void setMinQuotesPerCheck(uint64_t n) { min_quotes_per_check_ = n; }

    // =========================================================================
    // ACCESSORS
    // =========================================================================
    
    const SymbolQuoteStats* getStats(const std::string& symbol) const {
        auto it = stats_.find(symbol);
        return (it != stats_.end()) ? &it->second : nullptr;
    }

    std::vector<std::string> getStarvedSymbols() const {
        std::vector<std::string> result;
        for (const auto& kv : stats_) {
            if (kv.second.starved) {
                result.push_back(kv.first);
            }
        }
        return result;
    }

    size_t trackedCount() const { return stats_.size(); }
    
    uint64_t totalQuotes() const {
        uint64_t total = 0;
        for (const auto& kv : stats_) {
            total += kv.second.quote_count;
        }
        return total;
    }

    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    
    void dumpStats() const {
        printf("\n[STARVATION-DETECTOR] Full Statistics:\n");
        printf("%-12s %10s %10s %12s %8s\n", 
               "Symbol", "Quotes", "Last(ms)", "Subscribed", "Starved");
        printf("------------------------------------------------------\n");
        
        for (const auto& kv : stats_) {
            const auto& sym = kv.first;
            const auto& s = kv.second;
            
            printf("%-12s %10lu %10lu %12lu %8s\n",
                   sym.c_str(),
                   s.quote_count,
                   s.last_quote_ms,
                   s.subscribed_at_ms,
                   s.starved ? "YES" : "no");
        }
        printf("\n");
    }

private:
    std::unordered_map<std::string, SymbolQuoteStats> stats_;
    
    uint64_t starvation_threshold_ms_;
    uint64_t check_interval_ms_;
    uint64_t min_quotes_per_check_;
    uint64_t last_check_ms_ = 0;
    
    ResubscribeCallback resubscribe_cb_;
};

} // namespace Chimera
