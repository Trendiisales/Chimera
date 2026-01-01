// =============================================================================
// FIXDebugLogger.hpp - FIX Protocol Debug Logger
// =============================================================================
// PURPOSE: Capture and analyze FIX subscription and market data flow
// 
// PROBLEM WE'RE SOLVING:
//   XAUUSD subscription appeared to work but only got 2 quotes
//   We need visibility into:
//   - What symbols we REQUESTED subscription for
//   - What symbols actually returned in 35=W (snapshots)
//   - Quote distribution per symbol
//   - Any rejection messages (35=3, 35=j)
//
// USAGE:
//   logger.onSendMDRequest("XAUUSD", 41);     // When sending 35=V
//   logger.onMDResponse("XAUUSD", 41, bid, ask);  // When receiving 35=W
//   logger.onReject(msgType, reason);         // When receiving 35=3 or 35=j
//   logger.periodicReport(now_ms);            // Every 30s
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ctime>

namespace Chimera {

struct FIXSubscriptionState {
    std::string symbol;
    int security_id = 0;
    uint64_t requested_at_ms = 0;
    uint64_t first_quote_ms = 0;
    uint64_t last_quote_ms = 0;
    uint64_t quote_count = 0;
    bool acknowledged = false;
    bool rejected = false;
    std::string reject_reason;
};

class FIXDebugLogger {
public:
    explicit FIXDebugLogger(const std::string& log_path = "") {
        if (!log_path.empty()) {
            file_.open(log_path, std::ios::out | std::ios::app);
            if (file_.is_open()) {
                file_ << "\n=== FIX Debug Session Started ===\n";
            }
        }
    }

    ~FIXDebugLogger() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // =========================================================================
    // SUBSCRIPTION TRACKING
    // =========================================================================

    void onSecurityListReceived(int total_symbols) {
        log("SECLIST received: %d symbols available", total_symbols);
        security_list_size_ = total_symbols;
    }

    void onSecurityMapping(const std::string& symbol, int security_id) {
        security_id_map_[symbol] = security_id;
        reverse_id_map_[security_id] = symbol;
        log("SECMAP %s -> ID=%d", symbol.c_str(), security_id);
    }

    void onSendMDRequest(const std::string& symbol, int security_id, uint64_t now_ms) {
        auto& s = subscriptions_[symbol];
        s.symbol = symbol;
        s.security_id = security_id;
        s.requested_at_ms = now_ms;
        s.acknowledged = false;
        s.rejected = false;
        
        requested_symbols_.insert(symbol);
        
        log("MD-REQUEST %s (ID=%d) @ %lu ms", symbol.c_str(), security_id, now_ms);
    }

    void onMDResponse(const std::string& symbol, int security_id, 
                      double bid, double ask, uint64_t now_ms) {
        // Resolve symbol from security_id if needed
        std::string resolved_sym = symbol;
        if (resolved_sym.empty() && security_id > 0) {
            auto it = reverse_id_map_.find(security_id);
            if (it != reverse_id_map_.end()) {
                resolved_sym = it->second;
            } else {
                log("WARNING: Unknown security_id=%d in MD response", security_id);
                return;
            }
        }

        auto& s = subscriptions_[resolved_sym];
        if (s.quote_count == 0) {
            s.first_quote_ms = now_ms;
            s.acknowledged = true;
            log("FIRST-QUOTE %s bid=%.5f ask=%.5f (delay=%lu ms from request)",
                resolved_sym.c_str(), bid, ask, 
                s.requested_at_ms > 0 ? (now_ms - s.requested_at_ms) : 0);
        }
        
        s.last_quote_ms = now_ms;
        s.quote_count++;
        
        responding_symbols_.insert(resolved_sym);
    }

    void onMDResponseById(int security_id, double bid, double ask, uint64_t now_ms) {
        auto it = reverse_id_map_.find(security_id);
        if (it != reverse_id_map_.end()) {
            onMDResponse(it->second, security_id, bid, ask, now_ms);
        } else {
            log("MD-RESPONSE unknown ID=%d bid=%.5f ask=%.5f", security_id, bid, ask);
            unknown_ids_.insert(security_id);
        }
    }

    void onReject(const std::string& msg_type, int ref_seq, const std::string& reason) {
        reject_count_++;
        log("REJECT type=%s seq=%d reason=%s", msg_type.c_str(), ref_seq, reason.c_str());
        
        // Try to correlate with subscription
        // (This is approximate - FIX reject correlation can be tricky)
    }

    void onBusinessReject(const std::string& symbol, const std::string& reason) {
        if (!symbol.empty()) {
            auto& s = subscriptions_[symbol];
            s.rejected = true;
            s.reject_reason = reason;
        }
        log("BUSINESS-REJECT %s: %s", symbol.c_str(), reason.c_str());
    }

    // =========================================================================
    // PERIODIC REPORTING
    // =========================================================================

    void periodicReport(uint64_t now_ms) {
        if (now_ms - last_report_ms_ < report_interval_ms_) return;
        last_report_ms_ = now_ms;

        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║           FIX SUBSCRIPTION DEBUG REPORT                       ║\n");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        // Summary
        printf("║ Requested: %zu | Responding: %zu | Rejects: %lu                \n",
               requested_symbols_.size(), responding_symbols_.size(), reject_count_);
        
        // Find missing symbols
        std::vector<std::string> missing;
        for (const auto& sym : requested_symbols_) {
            if (responding_symbols_.find(sym) == responding_symbols_.end()) {
                missing.push_back(sym);
            }
        }
        
        if (!missing.empty()) {
            printf("╠══════════════════════════════════════════════════════════════╣\n");
            printf("║ ⚠️  MISSING (requested but no quotes):                        ║\n");
            for (const auto& sym : missing) {
                printf("║    → %s                                                   \n", sym.c_str());
            }
        }
        
        // Quote distribution
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║ %-12s %12s %12s %12s         ║\n", "Symbol", "Quotes", "First(ms)", "Last(ms)");
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        // Sort by quote count for easy identification
        std::vector<std::pair<std::string, uint64_t>> sorted;
        for (const auto& kv : subscriptions_) {
            sorted.push_back({kv.first, kv.second.quote_count});
        }
        std::sort(sorted.begin(), sorted.end(), 
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (const auto& p : sorted) {
            const auto& s = subscriptions_[p.first];
            const char* status = s.rejected ? "REJ" : (s.quote_count == 0 ? "ZERO" : "OK");
            printf("║ %-12s %12lu %12lu %12lu [%s]   ║\n",
                   p.first.c_str(),
                   s.quote_count,
                   s.first_quote_ms,
                   s.last_quote_ms,
                   status);
        }
        
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");

        // Write to file if open
        if (file_.is_open()) {
            file_ << "Report @ " << now_ms << " ms: "
                  << requested_symbols_.size() << " requested, "
                  << responding_symbols_.size() << " responding, "
                  << reject_count_ << " rejects\n";
            for (const auto& p : sorted) {
                const auto& s = subscriptions_[p.first];
                file_ << "  " << p.first << ": " << s.quote_count << " quotes\n";
            }
            file_.flush();
        }
    }

    // =========================================================================
    // RAW FIX MESSAGE LOGGING
    // =========================================================================

    void logRawFIX(const char* direction, const std::string& msg) {
        if (file_.is_open()) {
            file_ << "[" << direction << "] " << msg << "\n";
            file_.flush();
        }
    }

    // =========================================================================
    // ACCESSORS
    // =========================================================================

    std::vector<std::string> getMissingSymbols() const {
        std::vector<std::string> missing;
        for (const auto& sym : requested_symbols_) {
            if (responding_symbols_.find(sym) == responding_symbols_.end()) {
                missing.push_back(sym);
            }
        }
        return missing;
    }

    const FIXSubscriptionState* getState(const std::string& symbol) const {
        auto it = subscriptions_.find(symbol);
        return (it != subscriptions_.end()) ? &it->second : nullptr;
    }

    void setReportInterval(uint64_t ms) { report_interval_ms_ = ms; }

private:
    void log(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        printf("[FIX-DEBUG] %s\n", buf);
        
        if (file_.is_open()) {
            file_ << "[FIX-DEBUG] " << buf << "\n";
            file_.flush();
        }
    }

private:
    std::unordered_map<std::string, FIXSubscriptionState> subscriptions_;
    std::unordered_map<std::string, int> security_id_map_;
    std::unordered_map<int, std::string> reverse_id_map_;
    
    std::unordered_set<std::string> requested_symbols_;
    std::unordered_set<std::string> responding_symbols_;
    std::unordered_set<int> unknown_ids_;
    
    int security_list_size_ = 0;
    uint64_t reject_count_ = 0;
    uint64_t last_report_ms_ = 0;
    uint64_t report_interval_ms_ = 30000;  // 30 seconds
    
    std::ofstream file_;
};

} // namespace Chimera
