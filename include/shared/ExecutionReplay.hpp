// =============================================================================
// ExecutionReplay.hpp - v4.7.0 - EXECUTION DECISION LOGGING
// =============================================================================
// PURPOSE: Log every execution decision for post-session analysis
//
// After every session, you need to answer one question:
//   "Why did Chimera not trade?"
//
// This replay log tells you:
//   - What signals were seen
//   - What stopped them (BLOCKED, SUPPRESSED, MISSED)
//   - Whether thresholds are too strict
//   - Whether good opportunities were lost
//
// LOG FORMAT:
//   Append-only CSV with decision snapshots every 500ms when NOT trading
//   (That's where the information is - when we're NOT trading)
//
// OWNERSHIP: Jo
// LAST VERIFIED: 2025-01-01
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <deque>
#include <string>
#include <mutex>
#include "IntentGate.hpp"

namespace Chimera {

// =============================================================================
// Decision Log Entry
// =============================================================================
struct DecisionLogEntry {
    uint64_t ts_ns;
    char symbol[16];
    IntentState intent;
    double edge;
    double conviction;
    double spread_bps;
    bool ny_expansion;
    bool regime_stable;
    bool session_ok;
    TradeOutcome outcome;
    BlockReason reason;
    char failing_gates[64];  // Comma-separated list of failing checks
    
    void clear() {
        ts_ns = 0;
        symbol[0] = '\0';
        intent = IntentState::NO_TRADE;
        edge = 0.0;
        conviction = 0.0;
        spread_bps = 0.0;
        ny_expansion = false;
        regime_stable = false;
        session_ok = false;
        outcome = TradeOutcome::SUPPRESSED;
        reason = BlockReason::NONE;
        failing_gates[0] = '\0';
    }
    
    void setSymbol(const char* sym) {
        std::strncpy(symbol, sym, 15);
        symbol[15] = '\0';
    }
    
    void setFailingGates(const char* gates) {
        std::strncpy(failing_gates, gates, 63);
        failing_gates[63] = '\0';
    }
};

// =============================================================================
// Trade Statistics (per-symbol, per-session)
// =============================================================================
struct TradeStats {
    uint64_t executed = 0;
    uint64_t blocked = 0;
    uint64_t suppressed = 0;
    uint64_t missed = 0;
    
    // Edge statistics when blocked/suppressed
    double sum_edge_blocked = 0.0;
    double max_edge_blocked = 0.0;
    double sum_edge_missed = 0.0;
    double max_edge_missed = 0.0;
    
    // Block reason breakdown
    uint64_t blocked_intent = 0;
    uint64_t blocked_session = 0;
    uint64_t blocked_regime = 0;
    uint64_t blocked_spread = 0;
    uint64_t blocked_risk = 0;
    uint64_t blocked_other = 0;
    
    void record(TradeOutcome outcome, BlockReason reason, double edge) {
        switch (outcome) {
            case TradeOutcome::EXECUTED:
                executed++;
                break;
            case TradeOutcome::BLOCKED:
                blocked++;
                sum_edge_blocked += edge;
                max_edge_blocked = std::max(max_edge_blocked, edge);
                recordBlockReason(reason);
                break;
            case TradeOutcome::SUPPRESSED:
                suppressed++;
                break;
            case TradeOutcome::MISSED:
                missed++;
                sum_edge_missed += edge;
                max_edge_missed = std::max(max_edge_missed, edge);
                break;
        }
    }
    
    void recordBlockReason(BlockReason reason) {
        switch (reason) {
            case BlockReason::INTENT_NOT_LIVE:
                blocked_intent++;
                break;
            case BlockReason::SESSION_POLICY:
            case BlockReason::NY_NOT_EXPANDED:
                blocked_session++;
                break;
            case BlockReason::REGIME_TRANSITION:
                blocked_regime++;
                break;
            case BlockReason::SPREAD_TOO_WIDE:
            case BlockReason::SPREAD_TOO_THIN:
                blocked_spread++;
                break;
            case BlockReason::RISK_LIMIT:
            case BlockReason::DAILY_LOSS:
            case BlockReason::MAX_POSITION:
                blocked_risk++;
                break;
            default:
                blocked_other++;
                break;
        }
    }
    
    double avgEdgeBlocked() const {
        return blocked > 0 ? sum_edge_blocked / blocked : 0.0;
    }
    
    double avgEdgeMissed() const {
        return missed > 0 ? sum_edge_missed / missed : 0.0;
    }
    
    void print() const {
        printf("  Executed: %lu\n", (unsigned long)executed);
        printf("  Blocked: %lu (avg_edge=%.2f max=%.2f)\n", 
               (unsigned long)blocked, avgEdgeBlocked(), max_edge_blocked);
        printf("    - Intent: %lu\n", (unsigned long)blocked_intent);
        printf("    - Session: %lu\n", (unsigned long)blocked_session);
        printf("    - Regime: %lu\n", (unsigned long)blocked_regime);
        printf("    - Spread: %lu\n", (unsigned long)blocked_spread);
        printf("    - Risk: %lu\n", (unsigned long)blocked_risk);
        printf("    - Other: %lu\n", (unsigned long)blocked_other);
        printf("  Suppressed: %lu\n", (unsigned long)suppressed);
        printf("  Missed: %lu (avg_edge=%.2f max=%.2f)\n",
               (unsigned long)missed, avgEdgeMissed(), max_edge_missed);
    }
};

// =============================================================================
// Execution Replay Logger
// =============================================================================
class ExecutionReplayLogger {
public:
    struct Config {
        std::string log_path = "chimera_decisions.csv";
        uint64_t snapshot_interval_ns = 500'000'000;  // 500ms when not trading
        size_t max_buffer_size = 1000;
        bool enabled = true;
    };
    
    ExecutionReplayLogger() {
        initCSV();
    }
    
    ~ExecutionReplayLogger() {
        flush();
        if (csv_file_.is_open()) {
            csv_file_.close();
        }
    }
    
    void setConfig(const Config& cfg) { 
        config_ = cfg;
        if (!csv_file_.is_open() && config_.enabled) {
            initCSV();
        }
    }
    
    const Config& config() const { return config_; }
    
    // =========================================================================
    // LOG DECISION
    // =========================================================================
    void log(const DecisionLogEntry& entry) {
        if (!config_.enabled) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Add to buffer
        buffer_.push_back(entry);
        
        // Update stats
        auto& stats = symbol_stats_[entry.symbol];
        stats.record(entry.outcome, entry.reason, entry.edge);
        
        // Flush if buffer full
        if (buffer_.size() >= config_.max_buffer_size) {
            flushUnlocked();
        }
    }
    
    // =========================================================================
    // LOG DECISION SNAPSHOT (call every 500ms when not trading)
    // =========================================================================
    void logSnapshot(
        const char* symbol,
        IntentState intent,
        double edge,
        double conviction,
        double spread_bps,
        bool ny_expansion,
        bool regime_stable,
        bool session_ok,
        BlockReason primary_block_reason,
        const char* failing_gates,
        uint64_t ts_ns
    ) {
        // Determine outcome based on context
        TradeOutcome outcome;
        if (primary_block_reason == BlockReason::NONE) {
            outcome = TradeOutcome::SUPPRESSED;  // Engine idle
        } else if (primary_block_reason == BlockReason::EDGE_DECAYED) {
            outcome = TradeOutcome::MISSED;
        } else {
            outcome = TradeOutcome::BLOCKED;
        }
        
        DecisionLogEntry entry;
        entry.clear();
        entry.ts_ns = ts_ns;
        entry.setSymbol(symbol);
        entry.intent = intent;
        entry.edge = edge;
        entry.conviction = conviction;
        entry.spread_bps = spread_bps;
        entry.ny_expansion = ny_expansion;
        entry.regime_stable = regime_stable;
        entry.session_ok = session_ok;
        entry.outcome = outcome;
        entry.reason = primary_block_reason;
        entry.setFailingGates(failing_gates);
        
        log(entry);
    }
    
    // =========================================================================
    // LOG EXECUTED TRADE
    // =========================================================================
    void logExecuted(
        const char* symbol,
        IntentState intent,
        double edge,
        double conviction,
        double spread_bps,
        uint64_t ts_ns
    ) {
        DecisionLogEntry entry;
        entry.clear();
        entry.ts_ns = ts_ns;
        entry.setSymbol(symbol);
        entry.intent = intent;
        entry.edge = edge;
        entry.conviction = conviction;
        entry.spread_bps = spread_bps;
        entry.ny_expansion = true;
        entry.regime_stable = true;
        entry.session_ok = true;
        entry.outcome = TradeOutcome::EXECUTED;
        entry.reason = BlockReason::NONE;
        entry.setFailingGates("");
        
        log(entry);
    }
    
    // =========================================================================
    // FLUSH TO FILE
    // =========================================================================
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        flushUnlocked();
    }
    
    // =========================================================================
    // GET STATS
    // =========================================================================
    const TradeStats* getStats(const char* symbol) const {
        auto it = symbol_stats_.find(symbol);
        if (it == symbol_stats_.end()) return nullptr;
        return &it->second;
    }
    
    // =========================================================================
    // PRINT SESSION SUMMARY
    // =========================================================================
    void printSessionSummary() const {
        printf("\n═══════════════════════════════════════════════════════════════\n");
        printf("EXECUTION REPLAY SUMMARY\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        
        // Calculate totals
        TradeStats total;
        for (const auto& [symbol, stats] : symbol_stats_) {
            total.executed += stats.executed;
            total.blocked += stats.blocked;
            total.suppressed += stats.suppressed;
            total.missed += stats.missed;
            total.blocked_intent += stats.blocked_intent;
            total.blocked_session += stats.blocked_session;
            total.blocked_regime += stats.blocked_regime;
            total.blocked_spread += stats.blocked_spread;
            total.blocked_risk += stats.blocked_risk;
            total.blocked_other += stats.blocked_other;
            if (stats.max_edge_blocked > total.max_edge_blocked) {
                total.max_edge_blocked = stats.max_edge_blocked;
            }
            if (stats.max_edge_missed > total.max_edge_missed) {
                total.max_edge_missed = stats.max_edge_missed;
            }
        }
        
        printf("\nTOTAL:\n");
        total.print();
        
        printf("\nPER-SYMBOL:\n");
        for (const auto& [symbol, stats] : symbol_stats_) {
            printf("\n%s:\n", symbol.c_str());
            stats.print();
        }
        
        printf("\n═══════════════════════════════════════════════════════════════\n");
        
        // Analysis
        if (total.missed > 0) {
            printf("⚠️  MISSED TRADES: %lu opportunities lost (max edge %.2f)\n",
                   (unsigned long)total.missed, total.max_edge_missed);
            printf("   → Consider relaxing thresholds\n");
        }
        
        if (total.blocked_intent > total.executed && total.blocked > 10) {
            printf("⚠️  INTENT BLOCKED: %lu signals had edge but no LIVE intent\n",
                   (unsigned long)total.blocked_intent);
            printf("   → Review intent thresholds\n");
        }
        
        if (total.blocked_session > total.executed * 2) {
            printf("⚠️  SESSION BLOCKED: Many signals blocked by session rules\n");
            printf("   → Check if session windows are too narrow\n");
        }
        
        if (total.executed == 0 && total.blocked > 0) {
            printf("❌ NO TRADES EXECUTED despite %lu blocked signals\n",
                   (unsigned long)total.blocked);
            printf("   → Gates are working but may be too strict\n");
        }
        
        if (total.executed > 0 && total.blocked < total.executed / 2) {
            printf("✅ GOOD EXECUTION RATE: %lu trades with only %lu blocks\n",
                   (unsigned long)total.executed, (unsigned long)total.blocked);
        }
    }
    
    // =========================================================================
    // RESET (call at session start)
    // =========================================================================
    void resetSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        symbol_stats_.clear();
        buffer_.clear();
    }

private:
    Config config_;
    std::ofstream csv_file_;
    std::deque<DecisionLogEntry> buffer_;
    std::unordered_map<std::string, TradeStats> symbol_stats_;
    std::mutex mutex_;
    bool csv_initialized_ = false;
    
    void initCSV() {
        if (csv_initialized_) return;
        
        csv_file_.open(config_.log_path, std::ios::out | std::ios::trunc);
        if (csv_file_.is_open()) {
            // Write header
            csv_file_ << "timestamp,symbol,intent,edge,conviction,spread_bps,"
                      << "ny_expansion,regime_stable,session_ok,outcome,reason,"
                      << "failing_gates\n";
            csv_file_.flush();
            csv_initialized_ = true;
            printf("[REPLAY] Decision logging initialized: %s\n", config_.log_path.c_str());
        } else {
            printf("[REPLAY] WARNING: Could not open log file: %s\n", config_.log_path.c_str());
        }
    }
    
    void flushUnlocked() {
        if (!csv_file_.is_open() || buffer_.empty()) return;
        
        for (const auto& entry : buffer_) {
            // Format timestamp
            auto ns = entry.ts_ns;
            auto secs = ns / 1000000000ULL;
            auto ms = (ns / 1000000ULL) % 1000;
            
            csv_file_ << secs << "." << std::setfill('0') << std::setw(3) << ms << ","
                      << entry.symbol << ","
                      << intent_state_str(entry.intent) << ","
                      << std::fixed << std::setprecision(2) << entry.edge << ","
                      << entry.conviction << ","
                      << std::setprecision(1) << entry.spread_bps << ","
                      << (entry.ny_expansion ? "1" : "0") << ","
                      << (entry.regime_stable ? "1" : "0") << ","
                      << (entry.session_ok ? "1" : "0") << ","
                      << trade_outcome_str(entry.outcome) << ","
                      << block_reason_str(entry.reason) << ","
                      << entry.failing_gates << "\n";
        }
        
        csv_file_.flush();
        buffer_.clear();
    }
};

// =============================================================================
// GLOBAL REPLAY LOGGER ACCESS
// =============================================================================
inline ExecutionReplayLogger& getReplayLogger() {
    static ExecutionReplayLogger instance;
    return instance;
}

} // namespace Chimera
