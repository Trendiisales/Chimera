// =============================================================================
// MLAttribution.hpp - Per-Trade ML Attribution Logger (v4.6.0)
// =============================================================================
// PURPOSE: Log ML decisions with realized outcomes to prove ML value
// 
// WITHOUT THIS, ML IS BLIND.
// 
// Answers questions like:
//   - "Does ML add value in NY session on XAUUSD during BURST?"
//   - "Is q50 predictive of realized PnL?"
//   - "Which reject reasons are most common?"
// =============================================================================
#pragma once

#include "MLModel.hpp"
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <array>

namespace Chimera {
namespace ML {

// =============================================================================
// Attribution Record (logged per trade) - 64 bytes exactly
// =============================================================================
struct alignas(64) MLAttributionRecord {
    // ── Identification (8 bytes) ──
    uint64_t timestamp_ns;
    
    // ── Context (8 bytes) ──
    uint32_t symbol_id;
    int8_t   side;              // +1 BUY, -1 SELL
    Regime   regime;            // 1 byte
    Session  session;           // 1 byte
    uint8_t  decision;          // 0=REJECT, 1=ACCEPT
    
    // ── Decision Details (4 bytes) ──
    RejectReason reject_reason; // 1 byte
    uint8_t  padding[3];        // 3 bytes padding
    
    // ── Quantiles (20 bytes - floats for compactness) ──
    float q10;
    float q25;
    float q50;
    float q75;
    float q90;
    
    // ── Execution Context (8 bytes) ──
    float latency_us;
    float size_scale;
    
    // ── Outcomes (16 bytes) ──
    float realized_pnl;
    float mfe;                  // Max Favorable Excursion
    float mae;                  // Max Adverse Excursion
    uint32_t hold_time_ms;
    
    // Total: 8 + 8 + 4 + 20 + 8 + 16 = 64 bytes
    
    // Helpers
    bool isWin() const { return realized_pnl > 0.0f; }
    bool isLoss() const { return realized_pnl < 0.0f; }
    bool wasAccepted() const { return decision == 1; }
};

static_assert(sizeof(MLAttributionRecord) == 64, "MLAttributionRecord must be 64 bytes");

// =============================================================================
// Lock-Free Ring Buffer for Attribution Records
// =============================================================================
template<size_t CAPACITY>
class AttributionRingBuffer {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be power of 2");
    static constexpr size_t MASK = CAPACITY - 1;
    
public:
    bool push(const MLAttributionRecord& rec) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & MASK;
        
        if (next == tail_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        buffer_[head] = rec;
        head_.store(next, std::memory_order_release);
        return true;
    }
    
    bool pop(MLAttributionRecord& out) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        out = buffer_[tail];
        tail_.store((tail + 1) & MASK, std::memory_order_release);
        return true;
    }
    
    size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    
private:
    alignas(64) std::array<MLAttributionRecord, CAPACITY> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) std::atomic<size_t> dropped_{0};
};

// =============================================================================
// ML Attribution Logger
// =============================================================================
class MLAttributionLogger {
public:
    static constexpr size_t BUFFER_SIZE = 8192;
    
    explicit MLAttributionLogger(const char* path) noexcept 
        : running_(false), records_written_(0), fp_(nullptr) {
        std::strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';
    }
    
    ~MLAttributionLogger() { stop(); }
    
    // Non-copyable
    MLAttributionLogger(const MLAttributionLogger&) = delete;
    MLAttributionLogger& operator=(const MLAttributionLogger&) = delete;
    
    // =========================================================================
    // Lifecycle
    // =========================================================================
    bool start() noexcept {
        if (running_.load()) return true;
        
        fp_ = std::fopen(path_, "ab");
        if (!fp_) {
            std::fprintf(stderr, "[MLAttribution] Failed to open %s\n", path_);
            return false;
        }
        
        running_.store(true);
        writer_thread_ = std::thread(&MLAttributionLogger::writerLoop, this);
        
        std::printf("[MLAttribution] Started logging to %s\n", path_);
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        running_.store(false);
        
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        
        // Flush remaining
        if (fp_) {
            MLAttributionRecord rec;
            while (buffer_.pop(rec)) {
                std::fwrite(&rec, sizeof(MLAttributionRecord), 1, fp_);
                records_written_++;
            }
            std::fflush(fp_);
            std::fclose(fp_);
            fp_ = nullptr;
        }
        
        std::printf("[MLAttribution] Stopped. Records: %zu, Dropped: %zu\n",
            records_written_, buffer_.dropped());
    }
    
    // =========================================================================
    // Hot Path API
    // =========================================================================
    
    // Log trade entry (outcomes filled later via logClose)
    bool logEntry(
        uint32_t symbol_id,
        int8_t side,
        Regime regime,
        Session session,
        const MLQuantiles& q,
        MLGateDecision decision,
        RejectReason reject_reason,
        double latency_us,
        double size_scale
    ) noexcept {
        MLAttributionRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        
        auto now = std::chrono::steady_clock::now();
        rec.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        rec.symbol_id = symbol_id;
        rec.side = side;
        rec.regime = regime;
        rec.session = session;
        rec.decision = (decision == MLGateDecision::ACCEPT) ? 1 : 0;
        rec.reject_reason = reject_reason;
        
        rec.q10 = static_cast<float>(q.q10);
        rec.q25 = static_cast<float>(q.q25);
        rec.q50 = static_cast<float>(q.q50);
        rec.q75 = static_cast<float>(q.q75);
        rec.q90 = static_cast<float>(q.q90);
        
        rec.latency_us = static_cast<float>(latency_us);
        rec.size_scale = static_cast<float>(size_scale);
        
        // Outcomes zero (not yet known)
        rec.realized_pnl = 0.0f;
        rec.mfe = 0.0f;
        rec.mae = 0.0f;
        rec.hold_time_ms = 0;
        
        entries_logged_.fetch_add(1, std::memory_order_relaxed);
        
        // Track reject reasons
        if (decision == MLGateDecision::REJECT) {
            trackRejectReason(reject_reason);
        }
        
        return buffer_.push(rec);
    }
    
    // Log trade close with outcomes
    bool logClose(
        uint32_t symbol_id,
        int8_t side,
        Regime regime,
        Session session,
        const MLQuantiles& q,
        double latency_us,
        double size_scale,
        double realized_pnl,
        double mfe,
        double mae,
        uint32_t hold_time_ms
    ) noexcept {
        MLAttributionRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        
        auto now = std::chrono::steady_clock::now();
        rec.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        rec.symbol_id = symbol_id;
        rec.side = side;
        rec.regime = regime;
        rec.session = session;
        rec.decision = 1;  // Trade was accepted (it closed)
        rec.reject_reason = RejectReason::NONE;
        
        rec.q10 = static_cast<float>(q.q10);
        rec.q25 = static_cast<float>(q.q25);
        rec.q50 = static_cast<float>(q.q50);
        rec.q75 = static_cast<float>(q.q75);
        rec.q90 = static_cast<float>(q.q90);
        
        rec.latency_us = static_cast<float>(latency_us);
        rec.size_scale = static_cast<float>(size_scale);
        
        rec.realized_pnl = static_cast<float>(realized_pnl);
        rec.mfe = static_cast<float>(mfe);
        rec.mae = static_cast<float>(mae);
        rec.hold_time_ms = hold_time_ms;
        
        closes_logged_.fetch_add(1, std::memory_order_relaxed);
        if (realized_pnl > 0) wins_.fetch_add(1, std::memory_order_relaxed);
        else if (realized_pnl < 0) losses_.fetch_add(1, std::memory_order_relaxed);
        
        return buffer_.push(rec);
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    size_t recordsWritten() const noexcept { return records_written_; }
    size_t recordsDropped() const noexcept { return buffer_.dropped(); }
    uint64_t entriesLogged() const noexcept { return entries_logged_.load(); }
    uint64_t closesLogged() const noexcept { return closes_logged_.load(); }
    uint64_t wins() const noexcept { return wins_.load(); }
    uint64_t losses() const noexcept { return losses_.load(); }
    
    double winRate() const noexcept {
        uint64_t w = wins_.load();
        uint64_t l = losses_.load();
        uint64_t total = w + l;
        return total > 0 ? (100.0 * w / total) : 0.0;
    }
    
    // Reject reason breakdown
    uint64_t rejectsByReason(RejectReason r) const noexcept {
        size_t idx = static_cast<size_t>(r);
        if (idx < 10) return reject_counts_[idx].load();
        return 0;
    }
    
    void printStats() const {
        std::printf("[MLAttribution] entries=%lu closes=%lu wins=%lu losses=%lu (%.1f%%) written=%zu dropped=%zu\n",
            (unsigned long)entries_logged_.load(),
            (unsigned long)closes_logged_.load(),
            (unsigned long)wins_.load(),
            (unsigned long)losses_.load(),
            winRate(),
            records_written_,
            buffer_.dropped());
        
        // Print reject reason breakdown
        std::printf("[MLAttribution] Rejects: iqr=%lu tail=%lu tailspread=%lu edge=%lu lat=%lu dead=%lu drift=%lu\n",
            (unsigned long)reject_counts_[1].load(),  // IQR_TOO_NARROW
            (unsigned long)reject_counts_[2].load(),  // TAIL_RISK_HIGH
            (unsigned long)reject_counts_[9].load(),  // TAIL_SPREAD
            (unsigned long)reject_counts_[3].load(),  // EDGE_LOW
            (unsigned long)reject_counts_[4].load(),  // LATENCY_HIGH
            (unsigned long)reject_counts_[5].load(),  // DEAD_REGIME
            (unsigned long)(reject_counts_[6].load() + reject_counts_[7].load()));  // DRIFT
    }
    
private:
    void trackRejectReason(RejectReason r) {
        size_t idx = static_cast<size_t>(r);
        if (idx < 10) {
            reject_counts_[idx].fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void writerLoop() noexcept {
        MLAttributionRecord rec;
        
        while (running_.load(std::memory_order_relaxed)) {
            size_t written = 0;
            while (written < 100 && buffer_.pop(rec)) {
                std::fwrite(&rec, sizeof(MLAttributionRecord), 1, fp_);
                records_written_++;
                written++;
            }
            
            if (written > 0 && records_written_ % 1000 == 0) {
                std::fflush(fp_);
            }
            
            if (written == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    
private:
    AttributionRingBuffer<BUFFER_SIZE> buffer_;
    std::atomic<bool> running_;
    std::thread writer_thread_;
    size_t records_written_;
    FILE* fp_;
    char path_[256];
    
    std::atomic<uint64_t> entries_logged_{0};
    std::atomic<uint64_t> closes_logged_{0};
    std::atomic<uint64_t> wins_{0};
    std::atomic<uint64_t> losses_{0};
    
    // Reject reason counters (indexed by RejectReason enum)
    std::array<std::atomic<uint64_t>, 10> reject_counts_{};
};

// =============================================================================
// Global Attribution Logger
// =============================================================================
inline MLAttributionLogger& getMLAttributionLogger() {
    static MLAttributionLogger instance("ml_attribution.bin");
    return instance;
}

} // namespace ML
} // namespace Chimera
