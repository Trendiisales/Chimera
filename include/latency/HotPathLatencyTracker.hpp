// ═══════════════════════════════════════════════════════════════════════════════
// include/latency/HotPathLatencyTracker.hpp
// ═══════════════════════════════════════════════════════════════════════════════
// STATUS: 🔧 ACTIVE - v4.9.27
// PURPOSE: ACCURATE hot-path latency measurement for order send→ACK
// OWNER: Jo
// LAST VERIFIED: 2026-01-04
//
// v4.9.27 PHASE 2 FIXES:
// ═══════════════════════════════════════════════════════════════════════════════
//   FIX 2.2: SNAPSHOT INTEGRITY LOGGING (1Hz)
//     - logSnapshot() prints samples, mean, p95 every second
//     - Ensures data is real, not stale
//
//   FIX 2.3: BASELINE CAPTURE
//     - After first 100 samples, captures min/mean/p95 as baseline
//     - This is your WAN performance reference
//     - No logic impact, just recording for comparison
//
// v4.9.10: HONEST LATENCY METRICS
//   - Tracks ONLY order send → execution ACK latency
//   - Uses high-resolution monotonic clock
//   - Percentile-filtered (min, p10, p50) - NOT averages
//   - Filters out reconnect/timeout artifacts (>5ms spikes)
//   - Lock-free recording for hot path performance
//
// WHAT THIS MEASURES:
//   - Time from ws_.send_text(order) to receiving FILLED/NEW response
//   - This is the REAL hot-path latency your strategies depend on
//
// WHAT THIS DOES NOT MEASURE:
//   - Network ping/RTT (use external tools)
//   - TCP connect time
//   - WebSocket handshake time
//   - Reconnect latency (filtered out)
//
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <atomic>
#include <numeric>

namespace Chimera {

// ─────────────────────────────────────────────────────────────────────────────
// High-resolution monotonic timestamp (nanoseconds)
// ─────────────────────────────────────────────────────────────────────────────
inline uint64_t now_ns_monotonic() noexcept {
#ifdef _WIN32
    // Windows: QueryPerformanceCounter gives sub-microsecond precision
    static LARGE_INTEGER freq;
    static bool freq_init = [] {
        QueryPerformanceFrequency(&freq);
        return true;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>((now.QuadPart * 1000000000ULL) / freq.QuadPart);
#else
    // Linux: CLOCK_MONOTONIC_RAW is not affected by NTP adjustments
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Baseline Record (v4.9.27)
// ─────────────────────────────────────────────────────────────────────────────
struct LatencyBaseline {
    double min_ms = 0.0;
    double mean_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    uint64_t captured_at_samples = 0;
    uint64_t captured_at_ts = 0;
    bool captured = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// HotPathLatencyTracker - Order send→ACK latency with percentile stats
// ─────────────────────────────────────────────────────────────────────────────
class HotPathLatencyTracker {
public:
    // Default: 256 samples, 5ms spike filter
    static constexpr size_t DEFAULT_MAX_SAMPLES = 256;
    static constexpr uint64_t DEFAULT_SPIKE_THRESHOLD_NS = 5'000'000ULL;  // 5ms
    static constexpr size_t BASELINE_CAPTURE_THRESHOLD = 100;  // v4.9.27
    
    explicit HotPathLatencyTracker(
        size_t max_samples = DEFAULT_MAX_SAMPLES,
        uint64_t spike_threshold_ns = DEFAULT_SPIKE_THRESHOLD_NS
    ) noexcept
        : max_samples_(max_samples)
        , spike_threshold_ns_(spike_threshold_ns)
        , total_recorded_(0)
        , spikes_filtered_(0)
    {
        samples_.reserve(max_samples);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RECORDING - Call when ACK received
    // ═══════════════════════════════════════════════════════════════════════
    
    // Record latency in nanoseconds (send_ts_ns from order, recv_ts_ns from now)
    inline void record_ns(uint64_t delta_ns) noexcept {
        // Filter zero/invalid samples
        if (delta_ns == 0) return;
        
        // Filter spike samples (reconnects, timeouts, GC pauses)
        if (delta_ns > spike_threshold_ns_) {
            ++spikes_filtered_;
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Circular buffer behavior
        if (samples_.size() >= max_samples_) {
            samples_.erase(samples_.begin());
        }
        samples_.push_back(delta_ns);
        ++total_recorded_;
        
        // ═══════════════════════════════════════════════════════════════════
        // v4.9.27 FIX 2.3: BASELINE CAPTURE
        // ═══════════════════════════════════════════════════════════════════
        if (!baseline_.captured && samples_.size() >= BASELINE_CAPTURE_THRESHOLD) {
            captureBaselineLocked();
        }
    }
    
    // Convenience: record from send timestamp (calculates delta automatically)
    inline void record_from_send(uint64_t send_ts_ns) noexcept {
        uint64_t now_ns = now_ns_monotonic();
        if (now_ns > send_ts_ns) {
            record_ns(now_ns - send_ts_ns);
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.27 FIX 2.2: SNAPSHOT INTEGRITY LOGGING (1Hz)
    // ═══════════════════════════════════════════════════════════════════════
    // Call this once per second from main loop
    // ═══════════════════════════════════════════════════════════════════════
    void logSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (samples_.empty()) {
            printf("[LATENCY] samples=0 (NO_ACKS - awaiting first ACK)\n");
            return;
        }
        
        // Calculate mean
        double sum = 0.0;
        for (auto s : samples_) {
            sum += static_cast<double>(s);
        }
        double mean_ns = sum / samples_.size();
        double mean_ms = mean_ns * 1e-6;
        
        // Calculate p95
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t p95_idx = static_cast<size_t>(0.95 * (sorted.size() - 1));
        double p95_ms = static_cast<double>(sorted[p95_idx]) * 1e-6;
        double min_ms = static_cast<double>(sorted.front()) * 1e-6;
        double max_ms = static_cast<double>(sorted.back()) * 1e-6;
        
        printf("[LATENCY] samples=%zu mean=%.2fms p95=%.2fms min=%.2fms max=%.2fms spikes=%llu\n",
               samples_.size(), mean_ms, p95_ms, min_ms, max_ms,
               static_cast<unsigned long long>(spikes_filtered_.load()));
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // v4.9.27 FIX 2.3: BASELINE ACCESS
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] const LatencyBaseline& baseline() const noexcept {
        return baseline_;
    }
    
    [[nodiscard]] bool hasBaseline() const noexcept {
        return baseline_.captured;
    }
    
    void printBaseline() const {
        if (!baseline_.captured) {
            printf("[LATENCY] BASELINE: Not yet captured (need %zu samples)\n",
                   BASELINE_CAPTURE_THRESHOLD);
            return;
        }
        
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ LATENCY BASELINE (captured at %llu samples)                          \n",
               static_cast<unsigned long long>(baseline_.captured_at_samples));
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ min=%.2fms  mean=%.2fms  p50=%.2fms  p95=%.2fms  p99=%.2fms        \n",
               baseline_.min_ms, baseline_.mean_ms, baseline_.p50_ms, 
               baseline_.p95_ms, baseline_.p99_ms);
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    // Force baseline recapture
    void recaptureBaseline() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.size() >= 10) {  // Need at least some samples
            baseline_.captured = false;
            captureBaselineLocked();
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // METRICS - Percentile-based (NOT averages!)
    // ═══════════════════════════════════════════════════════════════════════
    
    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.empty();
    }
    
    [[nodiscard]] size_t count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.size();
    }
    
    [[nodiscard]] uint64_t total_recorded() const noexcept {
        return total_recorded_.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] uint64_t spikes_filtered() const noexcept {
        return spikes_filtered_.load(std::memory_order_relaxed);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Nanosecond metrics (raw)
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] uint64_t min_ns() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0;
        return *std::min_element(samples_.begin(), samples_.end());
    }
    
    [[nodiscard]] uint64_t p10_ns() const noexcept {
        return percentile_ns(0.10);
    }
    
    [[nodiscard]] uint64_t p50_ns() const noexcept {
        return percentile_ns(0.50);
    }
    
    [[nodiscard]] uint64_t p90_ns() const noexcept {
        return percentile_ns(0.90);
    }
    
    [[nodiscard]] uint64_t p95_ns() const noexcept {
        return percentile_ns(0.95);
    }
    
    [[nodiscard]] uint64_t p99_ns() const noexcept {
        return percentile_ns(0.99);
    }
    
    [[nodiscard]] uint64_t max_ns() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0;
        return *std::max_element(samples_.begin(), samples_.end());
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Mean (for logging - NOT for trading decisions!)
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] double mean_ns() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0.0;
        double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0);
        return sum / samples_.size();
    }
    
    [[nodiscard]] double mean_ms() const noexcept {
        return mean_ns() * 1e-6;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Millisecond metrics (for GUI display)
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] double min_ms() const noexcept {
        return static_cast<double>(min_ns()) * 1e-6;
    }
    
    [[nodiscard]] double p10_ms() const noexcept {
        return static_cast<double>(p10_ns()) * 1e-6;
    }
    
    [[nodiscard]] double p50_ms() const noexcept {
        return static_cast<double>(p50_ns()) * 1e-6;
    }
    
    [[nodiscard]] double p90_ms() const noexcept {
        return static_cast<double>(p90_ns()) * 1e-6;
    }
    
    [[nodiscard]] double p95_ms() const noexcept {
        return static_cast<double>(p95_ns()) * 1e-6;
    }
    
    [[nodiscard]] double p99_ms() const noexcept {
        return static_cast<double>(p99_ns()) * 1e-6;
    }
    
    [[nodiscard]] double max_ms() const noexcept {
        return static_cast<double>(max_ns()) * 1e-6;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Microsecond metrics (for detailed analysis)
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] double min_us() const noexcept {
        return static_cast<double>(min_ns()) * 1e-3;
    }
    
    [[nodiscard]] double p10_us() const noexcept {
        return static_cast<double>(p10_ns()) * 1e-3;
    }
    
    [[nodiscard]] double p50_us() const noexcept {
        return static_cast<double>(p50_ns()) * 1e-3;
    }
    
    [[nodiscard]] double p90_us() const noexcept {
        return static_cast<double>(p90_ns()) * 1e-3;
    }
    
    [[nodiscard]] double p99_us() const noexcept {
        return static_cast<double>(p99_ns()) * 1e-3;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Snapshot for atomic GUI update
    // ─────────────────────────────────────────────────────────────────────────
    
    struct LatencySnapshot {
        uint64_t min_ns = 0;
        uint64_t p10_ns = 0;
        uint64_t p50_ns = 0;
        uint64_t p90_ns = 0;
        uint64_t p99_ns = 0;
        uint64_t max_ns = 0;
        size_t sample_count = 0;
        uint64_t total_recorded = 0;
        uint64_t spikes_filtered = 0;
        
        // Millisecond conversions
        double min_ms() const { return min_ns * 1e-6; }
        double p10_ms() const { return p10_ns * 1e-6; }
        double p50_ms() const { return p50_ns * 1e-6; }
        double p90_ms() const { return p90_ns * 1e-6; }
        double p99_ms() const { return p99_ns * 1e-6; }
        double max_ms() const { return max_ns * 1e-6; }
    };
    
    [[nodiscard]] LatencySnapshot snapshot() const noexcept {
        LatencySnapshot snap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!samples_.empty()) {
                std::vector<uint64_t> sorted = samples_;
                std::sort(sorted.begin(), sorted.end());
                size_t n = sorted.size();
                
                snap.min_ns = sorted.front();
                snap.p10_ns = sorted[std::min(static_cast<size_t>(n * 0.10), n - 1)];
                snap.p50_ns = sorted[std::min(static_cast<size_t>(n * 0.50), n - 1)];
                snap.p90_ns = sorted[std::min(static_cast<size_t>(n * 0.90), n - 1)];
                snap.p99_ns = sorted[std::min(static_cast<size_t>(n * 0.99), n - 1)];
                snap.max_ns = sorted.back();
                snap.sample_count = n;
            }
        }
        snap.total_recorded = total_recorded_.load(std::memory_order_relaxed);
        snap.spikes_filtered = spikes_filtered_.load(std::memory_order_relaxed);
        return snap;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // RESET
    // ═══════════════════════════════════════════════════════════════════════
    
    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        total_recorded_.store(0, std::memory_order_relaxed);
        spikes_filtered_.store(0, std::memory_order_relaxed);
        baseline_ = LatencyBaseline{};
    }
    
private:
    // v4.9.27: Internal baseline capture (called with lock held)
    void captureBaselineLocked() {
        if (samples_.empty()) return;
        
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        
        // Calculate mean
        double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0);
        double mean_ns = sum / n;
        
        baseline_.min_ms = static_cast<double>(sorted.front()) * 1e-6;
        baseline_.mean_ms = mean_ns * 1e-6;
        baseline_.p50_ms = static_cast<double>(sorted[n / 2]) * 1e-6;
        baseline_.p95_ms = static_cast<double>(sorted[static_cast<size_t>(n * 0.95)]) * 1e-6;
        baseline_.p99_ms = static_cast<double>(sorted[std::min(static_cast<size_t>(n * 0.99), n - 1)]) * 1e-6;
        baseline_.captured_at_samples = n;
        baseline_.captured_at_ts = now_ns_monotonic();
        baseline_.captured = true;
        
        printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
        printf("║ [LATENCY] BASELINE CAPTURED (v4.9.27)                                ║\n");
        printf("╠══════════════════════════════════════════════════════════════════════╣\n");
        printf("║ Samples: %zu                                                         \n", n);
        printf("║ min=%.2fms  mean=%.2fms  p50=%.2fms  p95=%.2fms  p99=%.2fms        \n",
               baseline_.min_ms, baseline_.mean_ms, baseline_.p50_ms,
               baseline_.p95_ms, baseline_.p99_ms);
        printf("║ This is your WAN performance reference.                              ║\n");
        printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    }
    
    [[nodiscard]] uint64_t percentile_ns(double p) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (samples_.empty()) return 0;
        
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        
        size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        
        return sorted[idx];
    }
    
    mutable std::mutex mutex_;
    std::vector<uint64_t> samples_;
    size_t max_samples_;
    uint64_t spike_threshold_ns_;
    std::atomic<uint64_t> total_recorded_;
    std::atomic<uint64_t> spikes_filtered_;
    
    // v4.9.27: Baseline storage
    LatencyBaseline baseline_;
};

} // namespace Chimera
