// ═══════════════════════════════════════════════════════════════════════════════
// include/metrics/TradeOpportunityMetrics.hpp
// v4.2.2: "Why are we not trading?" visibility
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <string>
#include <sstream>
#include <unordered_map>
#include <mutex>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Block reasons (why we didn't trade)
// ─────────────────────────────────────────────────────────────────────────────
enum class BlockReason : uint8_t {
    NONE = 0,           // Trade fired
    NO_BURST,           // Not in burst window
    LOW_EDGE,           // Edge below threshold
    COOLDOWN,           // Cooldown active
    SPREAD_WIDE,        // Spread too wide
    SYMBOL_DISABLED,    // Symbol auto-disabled
    NEG_EXPECTANCY,     // Negative expectancy
    WARMUP,             // Still warming up
    POSITION_OPEN,      // Already have position
    FEED_STALE,         // Data feed stale
    SESSION_CLOSED,     // Outside trading hours
    OTHER               // Other reason
};

inline const char* block_reason_str(BlockReason r) {
    switch (r) {
        case BlockReason::NONE:           return "TRADED";
        case BlockReason::NO_BURST:       return "NO_BURST";
        case BlockReason::LOW_EDGE:       return "LOW_EDGE";
        case BlockReason::COOLDOWN:       return "COOLDOWN";
        case BlockReason::SPREAD_WIDE:    return "SPREAD_WIDE";
        case BlockReason::SYMBOL_DISABLED: return "SYMBOL_DISABLED";
        case BlockReason::NEG_EXPECTANCY: return "NEG_EXPECTANCY";
        case BlockReason::WARMUP:         return "WARMUP";
        case BlockReason::POSITION_OPEN:  return "POSITION_OPEN";
        case BlockReason::FEED_STALE:     return "FEED_STALE";
        case BlockReason::SESSION_CLOSED: return "SESSION_CLOSED";
        case BlockReason::OTHER:          return "OTHER";
    }
    return "UNKNOWN";
}

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
        ticks_total++;
        switch (reason) {
            case BlockReason::NONE:           traded++; break;
            case BlockReason::NO_BURST:       no_burst++; break;
            case BlockReason::LOW_EDGE:       low_edge++; break;
            case BlockReason::COOLDOWN:       cooldown++; break;
            case BlockReason::SPREAD_WIDE:    spread_wide++; break;
            case BlockReason::SYMBOL_DISABLED: symbol_disabled++; break;
            case BlockReason::NEG_EXPECTANCY: neg_expectancy++; break;
            case BlockReason::WARMUP:         warmup++; break;
            case BlockReason::POSITION_OPEN:  position_open++; break;
            case BlockReason::FEED_STALE:     feed_stale++; break;
            case BlockReason::SESSION_CLOSED: session_closed++; break;
            case BlockReason::OTHER:          other++; break;
        }
    }
    
    void reset() {
        no_burst = 0;
        low_edge = 0;
        cooldown = 0;
        spread_wide = 0;
        symbol_disabled = 0;
        neg_expectancy = 0;
        warmup = 0;
        position_open = 0;
        feed_stale = 0;
        session_closed = 0;
        other = 0;
        traded = 0;
        ticks_total = 0;
    }
    
    SymbolMetricsSnapshot snapshot() const {
        return {
            no_burst.load(),
            low_edge.load(),
            cooldown.load(),
            spread_wide.load(),
            symbol_disabled.load(),
            neg_expectancy.load(),
            warmup.load(),
            position_open.load(),
            feed_stale.load(),
            session_closed.load(),
            other.load(),
            traded.load(),
            ticks_total.load()
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Global metrics manager
// ─────────────────────────────────────────────────────────────────────────────
class TradeOpportunityMetrics {
public:
    static TradeOpportunityMetrics& instance() {
        static TradeOpportunityMetrics inst;
        return inst;
    }
    
    void record(const std::string& symbol, BlockReason reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_[symbol].record(reason);
        global_.record(reason);
    }
    
    // Prometheus format export
    std::string export_prometheus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream o;
        
        auto g = global_.snapshot();
        
        // Global metrics
        o << "# HELP chimera_block_reason Count of block reasons across all symbols\n";
        o << "# TYPE chimera_block_reason counter\n";
        o << "chimera_no_burst " << g.no_burst << "\n";
        o << "chimera_low_edge " << g.low_edge << "\n";
        o << "chimera_cooldown " << g.cooldown << "\n";
        o << "chimera_spread_wide " << g.spread_wide << "\n";
        o << "chimera_symbol_disabled " << g.symbol_disabled << "\n";
        o << "chimera_neg_expectancy " << g.neg_expectancy << "\n";
        o << "chimera_warmup " << g.warmup << "\n";
        o << "chimera_position_open " << g.position_open << "\n";
        o << "chimera_feed_stale " << g.feed_stale << "\n";
        o << "chimera_session_closed " << g.session_closed << "\n";
        o << "chimera_other " << g.other << "\n";
        o << "chimera_traded " << g.traded << "\n";
        o << "chimera_ticks_total " << g.ticks_total << "\n";
        
        // Per-symbol metrics
        o << "\n# HELP chimera_symbol_traded Trades per symbol\n";
        o << "# TYPE chimera_symbol_traded counter\n";
        for (const auto& [sym, m] : metrics_) {
            auto s = m.snapshot();
            o << "chimera_symbol_traded{symbol=\"" << sym << "\"} " 
              << s.traded << "\n";
            o << "chimera_symbol_low_edge{symbol=\"" << sym << "\"} " 
              << s.low_edge << "\n";
            o << "chimera_symbol_no_burst{symbol=\"" << sym << "\"} " 
              << s.no_burst << "\n";
        }
        
        return o.str();
    }
    
    // JSON format for dashboard
    std::string export_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream o;
        
        auto g = global_.snapshot();
        
        o << "{\"global\":{";
        o << "\"no_burst\":" << g.no_burst << ",";
        o << "\"low_edge\":" << g.low_edge << ",";
        o << "\"cooldown\":" << g.cooldown << ",";
        o << "\"spread_wide\":" << g.spread_wide << ",";
        o << "\"neg_expectancy\":" << g.neg_expectancy << ",";
        o << "\"warmup\":" << g.warmup << ",";
        o << "\"position_open\":" << g.position_open << ",";
        o << "\"traded\":" << g.traded << ",";
        o << "\"ticks_total\":" << g.ticks_total;
        o << "},\"symbols\":{";
        
        bool first = true;
        for (const auto& [sym, m] : metrics_) {
            auto s = m.snapshot();
            if (!first) o << ",";
            first = false;
            o << "\"" << sym << "\":{";
            o << "\"traded\":" << s.traded << ",";
            o << "\"low_edge\":" << s.low_edge << ",";
            o << "\"no_burst\":" << s.no_burst << ",";
            o << "\"ticks\":" << s.ticks_total;
            o << "}";
        }
        o << "}}";
        
        return o.str();
    }
    
    // Get snapshot for single symbol
    SymbolMetricsSnapshot get_symbol(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(symbol);
        if (it == metrics_.end())
            return SymbolMetricsSnapshot{};
        return it->second.snapshot();
    }
    
    // Get global snapshot
    SymbolMetricsSnapshot get_global() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return global_.snapshot();
    }
    
    // Calculate trade rate (trades per 10000 ticks)
    double trade_rate() const {
        auto g = global_.snapshot();
        if (g.ticks_total == 0) return 0.0;
        return double(g.traded) / g.ticks_total * 10000.0;
    }
    
    // Nightly reset
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        global_.reset();
        for (auto& [sym, m] : metrics_) {
            m.reset();
        }
    }

private:
    TradeOpportunityMetrics() = default;
    
    mutable std::mutex mutex_;
    SymbolMetrics global_;
    std::unordered_map<std::string, SymbolMetrics> metrics_;
};

// Convenience function
inline void record_block(const std::string& symbol, BlockReason reason) {
    TradeOpportunityMetrics::instance().record(symbol, reason);
}

} // namespace Chimera
