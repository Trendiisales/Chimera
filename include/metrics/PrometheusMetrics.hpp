#pragma once
// =============================================================================
// PrometheusMetrics.hpp v4.2.2 - Zero-Coupling Observability
// =============================================================================
// Lock-free metrics for Prometheus/Grafana integration.
// 
// THREE-TIER OBSERVABILITY:
//   Tier 0 - Counters (hot path, zero cost)
//   Tier 1 - Snapshots (sampling thread, fixed cost)
//   Tier 2 - HTTP/Dashboard (unlimited cost, isolated)
//
// CRITICAL: Search thread only touches Tier 0 (atomic increments).
// HTTP thread only reads Tier 1 snapshots.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <array>

namespace Omega {

// =============================================================================
// TIER 0: HOT-PATH COUNTERS (atomic, zero allocation)
// =============================================================================
struct alignas(64) HotPathCounters {
    // Search metrics
    std::atomic<uint64_t> search_ticks{0};
    std::atomic<uint64_t> bursts_detected{0};
    std::atomic<uint64_t> confirms_passed{0};
    std::atomic<uint64_t> trades_fired{0};
    
    // Execution metrics
    std::atomic<uint64_t> orders_sent{0};
    std::atomic<uint64_t> orders_filled{0};
    std::atomic<uint64_t> orders_rejected{0};
    std::atomic<uint64_t> orders_cancelled{0};
    
    // Risk metrics
    std::atomic<uint64_t> kill_switch_triggers{0};
    std::atomic<uint64_t> blocks_total{0};
    std::atomic<uint64_t> blocks_latency{0};
    std::atomic<uint64_t> blocks_spread{0};
    
    // PnL (in millibps for precision)
    std::atomic<int64_t> session_pnl_millibps{0};
    std::atomic<int64_t> total_pnl_millibps{0};
    
    // Latency (in nanoseconds)
    std::atomic<uint64_t> latency_sum_ns{0};
    std::atomic<uint64_t> latency_count{0};
    std::atomic<uint64_t> latency_max_ns{0};
};

// Global hot-path counters (singleton)
inline HotPathCounters& GetHotPathCounters() {
    static HotPathCounters counters;
    return counters;
}

// =============================================================================
// TIER 1: METRICS SNAPSHOT (copied by metrics thread, read by HTTP)
// =============================================================================
struct MetricsSnapshot {
    uint64_t timestamp_ns = 0;
    
    // Copied from hot-path
    uint64_t search_ticks = 0;
    uint64_t bursts_detected = 0;
    uint64_t confirms_passed = 0;
    uint64_t trades_fired = 0;
    
    uint64_t orders_sent = 0;
    uint64_t orders_filled = 0;
    uint64_t orders_rejected = 0;
    
    uint64_t kill_switch_triggers = 0;
    uint64_t blocks_total = 0;
    
    double session_pnl_bps = 0.0;
    double total_pnl_bps = 0.0;
    double latency_avg_ms = 0.0;
    double latency_max_ms = 0.0;
    
    // Computed
    double burst_to_confirm_ratio = 0.0;
    double confirm_to_trade_ratio = 0.0;
    double fill_rate = 0.0;
};

// =============================================================================
// LOCK-FREE SNAPSHOT BUFFER (double-buffer pattern)
// =============================================================================
class MetricsSnapshotBuffer {
public:
    // Called by metrics producer thread (Tier 1)
    void Publish(const MetricsSnapshot& snap) {
        uint64_t seq = sequence_.load(std::memory_order_relaxed);
        buffer_[seq & 1] = snap;
        sequence_.store(seq + 1, std::memory_order_release);
    }
    
    // Called by HTTP consumer thread (Tier 2)
    MetricsSnapshot Read() const {
        MetricsSnapshot snap;
        uint64_t seq;
        
        do {
            seq = sequence_.load(std::memory_order_acquire);
            snap = buffer_[seq & 1];
        } while (seq != sequence_.load(std::memory_order_acquire));
        
        return snap;
    }
    
    uint64_t Sequence() const {
        return sequence_.load(std::memory_order_relaxed);
    }
    
private:
    alignas(64) std::atomic<uint64_t> sequence_{0};
    alignas(64) MetricsSnapshot buffer_[2];
};

// Global snapshot buffer (singleton)
inline MetricsSnapshotBuffer& GetSnapshotBuffer() {
    static MetricsSnapshotBuffer buffer;
    return buffer;
}

// =============================================================================
// METRICS PRODUCER - Runs on dedicated thread, samples hot-path counters
// =============================================================================
inline MetricsSnapshot ProduceSnapshot(uint64_t now_ns) {
    const auto& hp = GetHotPathCounters();
    
    MetricsSnapshot snap;
    snap.timestamp_ns = now_ns;
    
    // Copy atomics
    snap.search_ticks = hp.search_ticks.load(std::memory_order_relaxed);
    snap.bursts_detected = hp.bursts_detected.load(std::memory_order_relaxed);
    snap.confirms_passed = hp.confirms_passed.load(std::memory_order_relaxed);
    snap.trades_fired = hp.trades_fired.load(std::memory_order_relaxed);
    
    snap.orders_sent = hp.orders_sent.load(std::memory_order_relaxed);
    snap.orders_filled = hp.orders_filled.load(std::memory_order_relaxed);
    snap.orders_rejected = hp.orders_rejected.load(std::memory_order_relaxed);
    
    snap.kill_switch_triggers = hp.kill_switch_triggers.load(std::memory_order_relaxed);
    snap.blocks_total = hp.blocks_total.load(std::memory_order_relaxed);
    
    // Convert millibps to bps
    snap.session_pnl_bps = hp.session_pnl_millibps.load(std::memory_order_relaxed) / 1000.0;
    snap.total_pnl_bps = hp.total_pnl_millibps.load(std::memory_order_relaxed) / 1000.0;
    
    // Compute latency
    uint64_t lat_count = hp.latency_count.load(std::memory_order_relaxed);
    if (lat_count > 0) {
        uint64_t lat_sum = hp.latency_sum_ns.load(std::memory_order_relaxed);
        snap.latency_avg_ms = (lat_sum / lat_count) / 1'000'000.0;
    }
    snap.latency_max_ms = hp.latency_max_ns.load(std::memory_order_relaxed) / 1'000'000.0;
    
    // Compute ratios
    if (snap.bursts_detected > 0) {
        snap.burst_to_confirm_ratio = double(snap.confirms_passed) / snap.bursts_detected;
    }
    if (snap.confirms_passed > 0) {
        snap.confirm_to_trade_ratio = double(snap.trades_fired) / snap.confirms_passed;
    }
    if (snap.orders_sent > 0) {
        snap.fill_rate = double(snap.orders_filled) / snap.orders_sent;
    }
    
    return snap;
}

// =============================================================================
// TIER 2: PROMETHEUS EXPORTER - Formats metrics for /metrics endpoint
// =============================================================================
inline std::string RenderPrometheus(const MetricsSnapshot& snap) {
    std::ostringstream out;
    
    // Search metrics
    out << "# HELP chimera_search_ticks_total Total search loop iterations\n";
    out << "# TYPE chimera_search_ticks_total counter\n";
    out << "chimera_search_ticks_total " << snap.search_ticks << "\n";
    
    out << "# HELP chimera_bursts_detected_total Total bursts detected\n";
    out << "# TYPE chimera_bursts_detected_total counter\n";
    out << "chimera_bursts_detected_total " << snap.bursts_detected << "\n";
    
    out << "# HELP chimera_confirms_passed_total Total confirmations passed\n";
    out << "# TYPE chimera_confirms_passed_total counter\n";
    out << "chimera_confirms_passed_total " << snap.confirms_passed << "\n";
    
    out << "# HELP chimera_trades_fired_total Total trades executed\n";
    out << "# TYPE chimera_trades_fired_total counter\n";
    out << "chimera_trades_fired_total " << snap.trades_fired << "\n";
    
    // Execution metrics
    out << "# HELP chimera_orders_sent_total Total orders sent\n";
    out << "# TYPE chimera_orders_sent_total counter\n";
    out << "chimera_orders_sent_total " << snap.orders_sent << "\n";
    
    out << "# HELP chimera_orders_filled_total Total orders filled\n";
    out << "# TYPE chimera_orders_filled_total counter\n";
    out << "chimera_orders_filled_total " << snap.orders_filled << "\n";
    
    // Risk metrics
    out << "# HELP chimera_kill_switch_triggers_total Total kill-switch triggers\n";
    out << "# TYPE chimera_kill_switch_triggers_total counter\n";
    out << "chimera_kill_switch_triggers_total " << snap.kill_switch_triggers << "\n";
    
    out << "# HELP chimera_blocks_total Total trade blocks\n";
    out << "# TYPE chimera_blocks_total counter\n";
    out << "chimera_blocks_total " << snap.blocks_total << "\n";
    
    // Gauges
    out << "# HELP chimera_session_pnl_bps Current session PnL in basis points\n";
    out << "# TYPE chimera_session_pnl_bps gauge\n";
    out << "chimera_session_pnl_bps " << std::fixed << std::setprecision(2) 
        << snap.session_pnl_bps << "\n";
    
    out << "# HELP chimera_latency_avg_ms Average latency in milliseconds\n";
    out << "# TYPE chimera_latency_avg_ms gauge\n";
    out << "chimera_latency_avg_ms " << std::fixed << std::setprecision(3) 
        << snap.latency_avg_ms << "\n";
    
    out << "# HELP chimera_latency_max_ms Maximum latency in milliseconds\n";
    out << "# TYPE chimera_latency_max_ms gauge\n";
    out << "chimera_latency_max_ms " << std::fixed << std::setprecision(3) 
        << snap.latency_max_ms << "\n";
    
    // Ratios
    out << "# HELP chimera_burst_to_confirm_ratio Ratio of confirms to bursts\n";
    out << "# TYPE chimera_burst_to_confirm_ratio gauge\n";
    out << "chimera_burst_to_confirm_ratio " << std::fixed << std::setprecision(3) 
        << snap.burst_to_confirm_ratio << "\n";
    
    out << "# HELP chimera_fill_rate Order fill rate\n";
    out << "# TYPE chimera_fill_rate gauge\n";
    out << "chimera_fill_rate " << std::fixed << std::setprecision(3) 
        << snap.fill_rate << "\n";
    
    return out.str();
}

// =============================================================================
// JSON EXPORTER - For dashboard
// =============================================================================
inline std::string RenderJson(const MetricsSnapshot& snap) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    
    out << "{\n";
    out << "  \"timestamp_ns\": " << snap.timestamp_ns << ",\n";
    out << "  \"search_ticks\": " << snap.search_ticks << ",\n";
    out << "  \"bursts_detected\": " << snap.bursts_detected << ",\n";
    out << "  \"confirms_passed\": " << snap.confirms_passed << ",\n";
    out << "  \"trades_fired\": " << snap.trades_fired << ",\n";
    out << "  \"orders_sent\": " << snap.orders_sent << ",\n";
    out << "  \"orders_filled\": " << snap.orders_filled << ",\n";
    out << "  \"orders_rejected\": " << snap.orders_rejected << ",\n";
    out << "  \"kill_switch_triggers\": " << snap.kill_switch_triggers << ",\n";
    out << "  \"blocks_total\": " << snap.blocks_total << ",\n";
    out << "  \"session_pnl_bps\": " << snap.session_pnl_bps << ",\n";
    out << "  \"total_pnl_bps\": " << snap.total_pnl_bps << ",\n";
    out << "  \"latency_avg_ms\": " << snap.latency_avg_ms << ",\n";
    out << "  \"latency_max_ms\": " << snap.latency_max_ms << ",\n";
    out << "  \"burst_to_confirm_ratio\": " << snap.burst_to_confirm_ratio << ",\n";
    out << "  \"confirm_to_trade_ratio\": " << snap.confirm_to_trade_ratio << ",\n";
    out << "  \"fill_rate\": " << snap.fill_rate << "\n";
    out << "}\n";
    
    return out.str();
}

// =============================================================================
// HOT-PATH HELPER MACROS - Use these in trading code
// =============================================================================
#define METRIC_INC(name) \
    Omega::GetHotPathCounters().name.fetch_add(1, std::memory_order_relaxed)

#define METRIC_ADD(name, val) \
    Omega::GetHotPathCounters().name.fetch_add(val, std::memory_order_relaxed)

#define METRIC_SET(name, val) \
    Omega::GetHotPathCounters().name.store(val, std::memory_order_relaxed)

#define METRIC_MAX(name, val) \
    do { \
        uint64_t old = Omega::GetHotPathCounters().name.load(std::memory_order_relaxed); \
        while (val > old && !Omega::GetHotPathCounters().name.compare_exchange_weak( \
            old, val, std::memory_order_relaxed, std::memory_order_relaxed)); \
    } while(0)

} // namespace Omega
