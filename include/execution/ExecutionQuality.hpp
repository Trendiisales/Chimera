// ═══════════════════════════════════════════════════════════════════════════════
// include/execution/ExecutionQuality.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.23
// PURPOSE: Execution quality telemetry - ground truth for trading performance
// OWNER: Jo
// CREATED: 2026-01-03
//
// This is LAYER A of the adaptive stack:
//   Turn raw ACKs / rejects into hard statistics the system can act on
//   and the GUI can explain.
//
// DESIGN:
//   - Per-symbol execution statistics
//   - Thread-safe atomic counters
//   - Latency percentiles (p50, p95, p99)
//   - Rolling window for recent performance
//   - No ML here - pure statistics
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <atomic>
#include <array>
#include <string>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include <cstdio>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// Per-Symbol Execution Statistics
// ─────────────────────────────────────────────────────────────────────────────
struct ExecStats {
    // Counters (lifetime)
    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> acked{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> fills{0};
    std::atomic<uint64_t> cancels{0};
    
    // Latency percentiles (microseconds) - updated on each ACK
    std::atomic<uint64_t> ack_min_us{UINT64_MAX};
    std::atomic<uint64_t> ack_p50_us{0};
    std::atomic<uint64_t> ack_p95_us{0};
    std::atomic<uint64_t> ack_p99_us{0};
    std::atomic<uint64_t> ack_max_us{0};
    
    // Rolling window for recent latencies
    static constexpr size_t LATENCY_WINDOW = 128;
    std::array<uint64_t, LATENCY_WINDOW> latency_ring{};
    std::atomic<size_t> latency_idx{0};
    std::atomic<size_t> latency_count{0};
    mutable std::mutex latency_mtx;
    
    // Quality metrics
    std::atomic<double> fill_rate{0.0};      // fills / sent
    std::atomic<double> reject_rate{0.0};    // rejected / sent
    std::atomic<double> timeout_rate{0.0};   // timeouts / sent
    
    void record_latency_us(uint64_t us) {
        std::lock_guard<std::mutex> lock(latency_mtx);
        
        size_t idx = latency_idx.load() % LATENCY_WINDOW;
        latency_ring[idx] = us;
        latency_idx.fetch_add(1);
        
        size_t count = latency_count.load();
        if (count < LATENCY_WINDOW) {
            latency_count.store(count + 1);
        }
        
        // Update min/max atomically
        uint64_t old_min = ack_min_us.load();
        while (us < old_min && !ack_min_us.compare_exchange_weak(old_min, us)) {}
        
        uint64_t old_max = ack_max_us.load();
        while (us > old_max && !ack_max_us.compare_exchange_weak(old_max, us)) {}
        
        // Recalculate percentiles
        recalc_percentiles_locked();
    }
    
    void recalc_percentiles_locked() {
        size_t count = std::min(latency_count.load(), LATENCY_WINDOW);
        if (count == 0) return;
        
        // Copy and sort
        std::array<uint64_t, LATENCY_WINDOW> sorted;
        std::copy(latency_ring.begin(), latency_ring.begin() + count, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + count);
        
        ack_p50_us.store(sorted[count * 50 / 100]);
        ack_p95_us.store(sorted[count * 95 / 100]);
        ack_p99_us.store(sorted[std::min(count - 1, count * 99 / 100)]);
    }
    
    void update_rates() {
        uint64_t s = sent.load();
        if (s == 0) return;
        
        fill_rate.store(static_cast<double>(fills.load()) / s);
        reject_rate.store(static_cast<double>(rejected.load()) / s);
        timeout_rate.store(static_cast<double>(timeouts.load()) / s);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Execution Quality Tracker (Singleton)
// ─────────────────────────────────────────────────────────────────────────────
class ExecutionQuality {
public:
    static constexpr size_t MAX_SYMBOLS = 64;
    
    static ExecutionQuality& instance() {
        static ExecutionQuality eq;
        return eq;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RECORD EVENTS
    // ═══════════════════════════════════════════════════════════════════════
    
    void record_send(const char* symbol) {
        ExecStats* s = get_or_create(symbol);
        if (s) {
            s->sent.fetch_add(1);
            s->update_rates();
        }
    }
    
    void record_ack(const char* symbol, uint64_t latency_us) {
        ExecStats* s = get_or_create(symbol);
        if (s) {
            s->acked.fetch_add(1);
            s->record_latency_us(latency_us);
            s->update_rates();
            
            // Log for visibility
            printf("[EXEC_QUALITY] %s ACK latency=%lluus (p50=%llu p95=%llu)\n",
                   symbol,
                   static_cast<unsigned long long>(latency_us),
                   static_cast<unsigned long long>(s->ack_p50_us.load()),
                   static_cast<unsigned long long>(s->ack_p95_us.load()));
        }
    }
    
    void record_reject(const char* symbol, const char* reason) {
        ExecStats* s = get_or_create(symbol);
        if (s) {
            s->rejected.fetch_add(1);
            s->update_rates();
            
            printf("[EXEC_QUALITY] %s REJECT: %s (total=%llu rate=%.2f%%)\n",
                   symbol, reason,
                   static_cast<unsigned long long>(s->rejected.load()),
                   s->reject_rate.load() * 100.0);
        }
    }
    
    void record_timeout(const char* symbol) {
        ExecStats* s = get_or_create(symbol);
        if (s) {
            s->timeouts.fetch_add(1);
            s->update_rates();
            
            printf("[EXEC_QUALITY] %s TIMEOUT (total=%llu rate=%.2f%%)\n",
                   symbol,
                   static_cast<unsigned long long>(s->timeouts.load()),
                   s->timeout_rate.load() * 100.0);
        }
    }
    
    void record_fill(const char* symbol) {
        ExecStats* s = get_or_create(symbol);
        if (s) {
            s->fills.fetch_add(1);
            s->update_rates();
        }
    }
    
    void record_cancel(const char* symbol) {
        ExecStats* s = get_or_create(symbol);
        if (s) {
            s->cancels.fetch_add(1);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // QUERY STATS
    // ═══════════════════════════════════════════════════════════════════════
    
    const ExecStats* stats(const char* symbol) const {
        for (size_t i = 0; i < symbol_count_.load(); ++i) {
            if (strcmp(symbols_[i], symbol) == 0) {
                return &stats_[i];
            }
        }
        return nullptr;
    }
    
    // Get summary for GUI
    struct Summary {
        uint64_t total_sent;
        uint64_t total_acked;
        uint64_t total_rejected;
        uint64_t total_timeouts;
        uint64_t total_fills;
        double avg_ack_p50_us;
        double avg_reject_rate;
    };
    
    Summary summary() const {
        Summary s{};
        size_t count = symbol_count_.load();
        uint64_t p50_sum = 0;
        double reject_sum = 0.0;
        size_t active = 0;
        
        for (size_t i = 0; i < count; ++i) {
            s.total_sent += stats_[i].sent.load();
            s.total_acked += stats_[i].acked.load();
            s.total_rejected += stats_[i].rejected.load();
            s.total_timeouts += stats_[i].timeouts.load();
            s.total_fills += stats_[i].fills.load();
            
            if (stats_[i].sent.load() > 0) {
                p50_sum += stats_[i].ack_p50_us.load();
                reject_sum += stats_[i].reject_rate.load();
                ++active;
            }
        }
        
        if (active > 0) {
            s.avg_ack_p50_us = static_cast<double>(p50_sum) / active;
            s.avg_reject_rate = reject_sum / active;
        }
        
        return s;
    }
    
    // Print all stats (for diagnostics)
    void print_all() const {
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ EXECUTION QUALITY TELEMETRY                                          ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        
        size_t count = symbol_count_.load();
        for (size_t i = 0; i < count; ++i) {
            const ExecStats& st = stats_[i];
            printf("║ %-10s sent=%5llu ack=%5llu rej=%3llu to=%3llu p50=%6lluus p95=%6lluus\n",
                   symbols_[i],
                   static_cast<unsigned long long>(st.sent.load()),
                   static_cast<unsigned long long>(st.acked.load()),
                   static_cast<unsigned long long>(st.rejected.load()),
                   static_cast<unsigned long long>(st.timeouts.load()),
                   static_cast<unsigned long long>(st.ack_p50_us.load()),
                   static_cast<unsigned long long>(st.ack_p95_us.load()));
        }
        
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }

private:
    ExecutionQuality() : symbol_count_(0) {
        for (auto& s : symbols_) s[0] = '\0';
    }
    
    ExecStats* get_or_create(const char* symbol) {
        // Check existing
        for (size_t i = 0; i < symbol_count_.load(); ++i) {
            if (strcmp(symbols_[i], symbol) == 0) {
                return &stats_[i];
            }
        }
        
        // Create new
        std::lock_guard<std::mutex> lock(create_mtx_);
        
        // Double-check after lock
        for (size_t i = 0; i < symbol_count_.load(); ++i) {
            if (strcmp(symbols_[i], symbol) == 0) {
                return &stats_[i];
            }
        }
        
        size_t idx = symbol_count_.load();
        if (idx >= MAX_SYMBOLS) {
            printf("[EXEC_QUALITY] ERROR: Max symbols exceeded\n");
            return nullptr;
        }
        
        strncpy(symbols_[idx], symbol, 15);
        symbols_[idx][15] = '\0';
        symbol_count_.fetch_add(1);
        
        return &stats_[idx];
    }
    
    std::array<char[16], MAX_SYMBOLS> symbols_;
    std::array<ExecStats, MAX_SYMBOLS> stats_;
    std::atomic<size_t> symbol_count_;
    std::mutex create_mtx_;
};

} // namespace Chimera
