// ═══════════════════════════════════════════════════════════════════════════════
// include/metrics/TradeOpportunityMetrics.hpp
// v4.8.0: "Why are we not trading?" visibility
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <string>
#include <sstream>
#include <unordered_map>
#include <mutex>

// Use unified enum definitions (BlockReason)
#include "../shared/ChimeraEnums.hpp"

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot for returning metrics (copyable)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolMetricsSnapshot {
    uint64_t no_burst = 0;
    uint64_t low_edge = 0;
    uint64_t cooldown = 0;
    uint64_t spread_wide = 0;
    uint64_t symbol_disabled = 0;
    uint64_t neg_expectancy = 0;
    uint64_t warmup = 0;
    uint64_t position_open = 0;
    uint64_t feed_stale = 0;
    uint64_t session_closed = 0;
    uint64_t other = 0;
    uint64_t traded = 0;
    uint64_t ticks_total = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-symbol metrics (thread-safe atomics)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolMetrics {
    std::atomic<uint64_t> no_burst{0};
    std::atomic<uint64_t> low_edge{0};
    std::atomic<uint64_t> cooldown{0};
    std::atomic<uint64_t> spread_wide{0};
    std::atomic<uint64_t> symbol_disabled{0};
    std::atomic<uint64_t> neg_expectancy{0};
    std::atomic<uint64_t> warmup{0};
    std::atomic<uint64_t> position_open{0};
    std::atomic<uint64_t> feed_stale{0};
    std::atomic<uint64_t> session_closed{0};
    std::atomic<uint64_t> other{0};
    std::atomic<uint64_t> traded{0};
    std::atomic<uint64_t> ticks_total{0};
    
    void record(BlockReason reason) {
        ticks_total.fetch_add(1, std::memory_order_relaxed);
        switch (reason) {
            case BlockReason::NONE:              traded.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::NO_BURST:
            case BlockReason::NY_NOT_EXPANDED:   no_burst.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::LOW_EDGE:
            case BlockReason::EDGE_BELOW_THRESH: low_edge.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::COOLDOWN:          cooldown.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::SPREAD_WIDE:
            case BlockReason::SPREAD_TOO_WIDE:   spread_wide.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::SYMBOL_DISABLED:   symbol_disabled.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::NEG_EXPECTANCY:    neg_expectancy.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::WARMUP:            warmup.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::POSITION_OPEN:
            case BlockReason::MAX_POSITION:      position_open.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::FEED_STALE:        feed_stale.fetch_add(1, std::memory_order_relaxed); break;
            case BlockReason::SESSION_CLOSED:
            case BlockReason::SESSION_POLICY:    session_closed.fetch_add(1, std::memory_order_relaxed); break;
            default:                             other.fetch_add(1, std::memory_order_relaxed); break;
        }
    }
    
    SymbolMetricsSnapshot snapshot() const {
        SymbolMetricsSnapshot s;
        s.no_burst = no_burst.load(std::memory_order_relaxed);
        s.low_edge = low_edge.load(std::memory_order_relaxed);
        s.cooldown = cooldown.load(std::memory_order_relaxed);
        s.spread_wide = spread_wide.load(std::memory_order_relaxed);
        s.symbol_disabled = symbol_disabled.load(std::memory_order_relaxed);
        s.neg_expectancy = neg_expectancy.load(std::memory_order_relaxed);
        s.warmup = warmup.load(std::memory_order_relaxed);
        s.position_open = position_open.load(std::memory_order_relaxed);
        s.feed_stale = feed_stale.load(std::memory_order_relaxed);
        s.session_closed = session_closed.load(std::memory_order_relaxed);
        s.other = other.load(std::memory_order_relaxed);
        s.traded = traded.load(std::memory_order_relaxed);
        s.ticks_total = ticks_total.load(std::memory_order_relaxed);
        return s;
    }
    
    void reset() {
        no_burst.store(0, std::memory_order_relaxed);
        low_edge.store(0, std::memory_order_relaxed);
        cooldown.store(0, std::memory_order_relaxed);
        spread_wide.store(0, std::memory_order_relaxed);
        symbol_disabled.store(0, std::memory_order_relaxed);
        neg_expectancy.store(0, std::memory_order_relaxed);
        warmup.store(0, std::memory_order_relaxed);
        position_open.store(0, std::memory_order_relaxed);
        feed_stale.store(0, std::memory_order_relaxed);
        session_closed.store(0, std::memory_order_relaxed);
        other.store(0, std::memory_order_relaxed);
        traded.store(0, std::memory_order_relaxed);
        ticks_total.store(0, std::memory_order_relaxed);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Global tracker (singleton pattern)
// ─────────────────────────────────────────────────────────────────────────────
class TradeOpportunityMetrics {
public:
    static TradeOpportunityMetrics& instance() {
        static TradeOpportunityMetrics inst;
        return inst;
    }
    
    void record(const char* symbol, BlockReason reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_[symbol].record(reason);
    }
    
    SymbolMetricsSnapshot getSnapshot(const char* symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(symbol);
        if (it != metrics_.end()) {
            return it->second.snapshot();
        }
        return SymbolMetricsSnapshot{};
    }
    
    std::string toJSON() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& [symbol, m] : metrics_) {
            if (!first) ss << ",";
            first = false;
            auto s = m.snapshot();
            ss << "\"" << symbol << "\":{";
            ss << "\"traded\":" << s.traded << ",";
            ss << "\"no_burst\":" << s.no_burst << ",";
            ss << "\"low_edge\":" << s.low_edge << ",";
            ss << "\"cooldown\":" << s.cooldown << ",";
            ss << "\"spread_wide\":" << s.spread_wide << ",";
            ss << "\"symbol_disabled\":" << s.symbol_disabled << ",";
            ss << "\"neg_expectancy\":" << s.neg_expectancy << ",";
            ss << "\"warmup\":" << s.warmup << ",";
            ss << "\"position_open\":" << s.position_open << ",";
            ss << "\"feed_stale\":" << s.feed_stale << ",";
            ss << "\"session_closed\":" << s.session_closed << ",";
            ss << "\"other\":" << s.other << ",";
            ss << "\"ticks_total\":" << s.ticks_total;
            ss << "}";
        }
        ss << "}";
        return ss.str();
    }
    
    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [symbol, m] : metrics_) {
            m.reset();
        }
    }
    
    void printSummary() {
        std::lock_guard<std::mutex> lock(mutex_);
        printf("\n[OPPORTUNITY-METRICS] ════════════════════════════════════════════\n");
        for (const auto& [symbol, m] : metrics_) {
            auto s = m.snapshot();
            if (s.ticks_total == 0) continue;
            
            double trade_pct = s.ticks_total > 0 ? 100.0 * s.traded / s.ticks_total : 0.0;
            printf("  %s: %.1f%% traded (%llu/%llu)\n", 
                   symbol.c_str(), trade_pct, 
                   static_cast<unsigned long long>(s.traded),
                   static_cast<unsigned long long>(s.ticks_total));
            
            if (s.no_burst > 0) printf("    - NO_BURST: %llu\n", static_cast<unsigned long long>(s.no_burst));
            if (s.low_edge > 0) printf("    - LOW_EDGE: %llu\n", static_cast<unsigned long long>(s.low_edge));
            if (s.cooldown > 0) printf("    - COOLDOWN: %llu\n", static_cast<unsigned long long>(s.cooldown));
            if (s.spread_wide > 0) printf("    - SPREAD_WIDE: %llu\n", static_cast<unsigned long long>(s.spread_wide));
            if (s.session_closed > 0) printf("    - SESSION_CLOSED: %llu\n", static_cast<unsigned long long>(s.session_closed));
            if (s.position_open > 0) printf("    - POSITION_OPEN: %llu\n", static_cast<unsigned long long>(s.position_open));
            if (s.warmup > 0) printf("    - WARMUP: %llu\n", static_cast<unsigned long long>(s.warmup));
            if (s.other > 0) printf("    - OTHER: %llu\n", static_cast<unsigned long long>(s.other));
        }
        printf("[OPPORTUNITY-METRICS] ════════════════════════════════════════════\n\n");
    }
    
private:
    TradeOpportunityMetrics() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, SymbolMetrics> metrics_;
};

inline TradeOpportunityMetrics& getOpportunityMetrics() {
    return TradeOpportunityMetrics::instance();
}

// ─────────────────────────────────────────────────────────────────────────────
// CONVENIENCE FUNCTION: record_block
// Used by PureScalper and other components to record block reasons
// ─────────────────────────────────────────────────────────────────────────────
inline void record_block(const char* symbol, BlockReason reason) {
    getOpportunityMetrics().record(symbol, reason);
}

// Overload for std::string
inline void record_block(const std::string& symbol, BlockReason reason) {
    getOpportunityMetrics().record(symbol.c_str(), reason);
}

} // namespace Chimera
