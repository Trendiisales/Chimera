// ═══════════════════════════════════════════════════════════════════════════════
// include/audit/ConvictionAudit.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// v4.9.11: SYMBOL CONVICTION HEATMAP
//
// PURPOSE: Prove where and why trades pass/fail by symbol, regime, session,
// and execution mode. This is audit-grade evidence that conviction logic works.
//
// TRACKS:
// - Conviction score per signal
// - Trade outcome (filled vs skipped)
// - Edge, latency, execution mode
// - Time of day (session effects)
// - Market regime
//
// OUTPUT:
// - CSV export for analysis
// - Summary statistics
// - Compliance evidence
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <ctime>

namespace Chimera {
namespace Audit {

// ─────────────────────────────────────────────────────────────────────────────
// Conviction Sample - Single decision point
// ─────────────────────────────────────────────────────────────────────────────
struct ConvictionSample {
    char symbol[16] = {0};
    int score = 0;                    // Raw conviction score (0-10)
    bool traded = false;              // Did we actually trade?
    bool filled = false;              // Did the order fill?
    double edge_bps = 0.0;            // Gross edge at decision
    double ack_p95_ms = 0.0;          // Latency at decision
    bool maker = false;               // Execution mode
    int hour_utc = 0;                 // Hour of day (0-23)
    
    // Market context
    char regime[16] = {0};            // TRENDING/RANGING/VOLATILE/DEAD
    char intent[16] = {0};            // MOMENTUM/MEAN_REVERSION/NO_TRADE
    double spread_bps = 0.0;
    double vpin = 0.0;
    double vol_z = 0.0;
    
    // Timestamp
    uint64_t ts_ns = 0;
    
    // Skip reason (if not traded)
    char skip_reason[32] = {0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Conviction Collector - Thread-safe sample storage
// ─────────────────────────────────────────────────────────────────────────────
class ConvictionCollector {
public:
    static constexpr size_t MAX_SAMPLES = 100000;
    
    static ConvictionCollector& instance() {
        static ConvictionCollector inst;
        return inst;
    }
    
    void record(const ConvictionSample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.size() >= MAX_SAMPLES) {
            // Rotate: remove oldest 10%
            samples_.erase(samples_.begin(), samples_.begin() + MAX_SAMPLES / 10);
        }
        samples_.push_back(sample);
    }
    
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }
    
    // Export to CSV
    void exportCSV(const char* path = "runtime/audit/conviction_heatmap.csv") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream f(path);
        if (!f.is_open()) return;
        
        // Header
        f << "symbol,score,traded,filled,edge_bps,ack_p95_ms,maker,hour_utc,"
          << "regime,intent,spread_bps,vpin,vol_z,skip_reason,ts_ns\n";
        
        // Data
        for (const auto& s : samples_) {
            f << s.symbol << ","
              << s.score << ","
              << (s.traded ? 1 : 0) << ","
              << (s.filled ? 1 : 0) << ","
              << s.edge_bps << ","
              << s.ack_p95_ms << ","
              << (s.maker ? 1 : 0) << ","
              << s.hour_utc << ","
              << s.regime << ","
              << s.intent << ","
              << s.spread_bps << ","
              << s.vpin << ","
              << s.vol_z << ","
              << s.skip_reason << ","
              << s.ts_ns << "\n";
        }
        
        f.close();
    }
    
    // Summary statistics
    struct Summary {
        size_t total_samples = 0;
        size_t traded_count = 0;
        size_t filled_count = 0;
        double avg_score_traded = 0.0;
        double avg_score_skipped = 0.0;
        double avg_edge_traded = 0.0;
        double trade_rate = 0.0;
        double fill_rate = 0.0;
    };
    
    Summary getSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Summary sum;
        sum.total_samples = samples_.size();
        
        double score_traded_sum = 0.0, score_skipped_sum = 0.0;
        double edge_traded_sum = 0.0;
        size_t skipped_count = 0;
        
        for (const auto& s : samples_) {
            if (s.traded) {
                sum.traded_count++;
                score_traded_sum += s.score;
                edge_traded_sum += s.edge_bps;
                if (s.filled) sum.filled_count++;
            } else {
                skipped_count++;
                score_skipped_sum += s.score;
            }
        }
        
        if (sum.traded_count > 0) {
            sum.avg_score_traded = score_traded_sum / sum.traded_count;
            sum.avg_edge_traded = edge_traded_sum / sum.traded_count;
        }
        if (skipped_count > 0) {
            sum.avg_score_skipped = score_skipped_sum / skipped_count;
        }
        if (sum.total_samples > 0) {
            sum.trade_rate = static_cast<double>(sum.traded_count) / sum.total_samples;
        }
        if (sum.traded_count > 0) {
            sum.fill_rate = static_cast<double>(sum.filled_count) / sum.traded_count;
        }
        
        return sum;
    }
    
    // Per-symbol breakdown
    struct SymbolStats {
        char symbol[16] = {0};
        size_t samples = 0;
        size_t traded = 0;
        size_t filled = 0;
        double avg_score = 0.0;
        double trade_rate = 0.0;
    };
    
    std::vector<SymbolStats> getPerSymbolStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Aggregate by symbol
        struct Agg { size_t samples = 0; size_t traded = 0; size_t filled = 0; double score_sum = 0.0; };
        std::vector<std::pair<std::string, Agg>> aggs;
        
        for (const auto& s : samples_) {
            bool found = false;
            for (auto& a : aggs) {
                if (a.first == s.symbol) {
                    a.second.samples++;
                    a.second.score_sum += s.score;
                    if (s.traded) a.second.traded++;
                    if (s.filled) a.second.filled++;
                    found = true;
                    break;
                }
            }
            if (!found) {
                Agg a;
                a.samples = 1;
                a.score_sum = s.score;
                if (s.traded) a.traded = 1;
                if (s.filled) a.filled = 1;
                aggs.push_back({s.symbol, a});
            }
        }
        
        // Convert to SymbolStats
        std::vector<SymbolStats> result;
        for (const auto& a : aggs) {
            SymbolStats ss;
            strncpy(ss.symbol, a.first.c_str(), 15);
            ss.samples = a.second.samples;
            ss.traded = a.second.traded;
            ss.filled = a.second.filled;
            ss.avg_score = a.second.samples > 0 ? a.second.score_sum / a.second.samples : 0.0;
            ss.trade_rate = a.second.samples > 0 ? static_cast<double>(a.second.traded) / a.second.samples : 0.0;
            result.push_back(ss);
        }
        
        return result;
    }
    
    // Per-hour breakdown (session analysis)
    struct HourStats {
        int hour = 0;
        size_t samples = 0;
        size_t traded = 0;
        double trade_rate = 0.0;
    };
    
    std::vector<HourStats> getPerHourStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<HourStats> hours(24);
        for (int i = 0; i < 24; i++) hours[i].hour = i;
        
        for (const auto& s : samples_) {
            if (s.hour_utc >= 0 && s.hour_utc < 24) {
                hours[s.hour_utc].samples++;
                if (s.traded) hours[s.hour_utc].traded++;
            }
        }
        
        for (auto& h : hours) {
            h.trade_rate = h.samples > 0 ? static_cast<double>(h.traded) / h.samples : 0.0;
        }
        
        return hours;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
    }
    
private:
    ConvictionCollector() = default;
    mutable std::mutex mutex_;
    std::vector<ConvictionSample> samples_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Record a conviction decision
// ─────────────────────────────────────────────────────────────────────────────
inline void recordConviction(
    const char* symbol,
    int score,
    bool traded,
    bool filled,
    double edge_bps,
    double ack_p95_ms,
    bool maker,
    const char* regime,
    const char* intent,
    double spread_bps,
    double vpin,
    double vol_z,
    const char* skip_reason,
    uint64_t ts_ns
) {
    ConvictionSample s;
    strncpy(s.symbol, symbol, 15);
    s.score = score;
    s.traded = traded;
    s.filled = filled;
    s.edge_bps = edge_bps;
    s.ack_p95_ms = ack_p95_ms;
    s.maker = maker;
    
    // Get UTC hour
    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);
    s.hour_utc = utc ? utc->tm_hour : 0;
    
    strncpy(s.regime, regime, 15);
    strncpy(s.intent, intent, 15);
    s.spread_bps = spread_bps;
    s.vpin = vpin;
    s.vol_z = vol_z;
    strncpy(s.skip_reason, skip_reason, 31);
    s.ts_ns = ts_ns;
    
    ConvictionCollector::instance().record(s);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Get global collector
// ─────────────────────────────────────────────────────────────────────────────
inline ConvictionCollector& getConvictionCollector() {
    return ConvictionCollector::instance();
}

} // namespace Audit
} // namespace Chimera
