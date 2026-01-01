// =============================================================================
// MLFeatureLogger.hpp - Hot-Path Safe Feature Logging
// =============================================================================
// PURPOSE: Log ML features to binary file for offline training
// DESIGN:
//   - Lock-free ring buffer for hot path
//   - Background thread writes to disk
//   - Binary format for speed, CSV export for analysis
//   - Zero allocations on hot path
//
// ARCHITECTURE:
//   Hot Path (μs critical):
//     Strategy → push(record) → ring buffer (atomic)
//   
//   Background (can be slow):
//     Ring buffer → disk write thread → binary file
//
// USAGE:
//   MLFeatureLogger logger("ml_features.bin");
//   logger.start();
//   
//   // In hot path:
//   MLFeatureRecord rec;
//   // ... fill record ...
//   logger.log(rec);  // Non-blocking, <100ns
//   
//   // On shutdown:
//   logger.stop();
// =============================================================================
#pragma once

#include "MLTypes.hpp"
#include <atomic>
#include <thread>
#include <cstdio>
#include <chrono>
#include <array>

namespace Chimera {
namespace ML {

// =============================================================================
// Lock-Free Ring Buffer for Feature Records
// =============================================================================
template<size_t CAPACITY>
class FeatureRingBuffer {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be power of 2");
    static constexpr size_t MASK = CAPACITY - 1;
    
public:
    FeatureRingBuffer() noexcept : head_(0), tail_(0) {
        // Zero-initialize all slots
        for (auto& slot : buffer_) {
            slot.timestamp_ns = 0;
        }
    }
    
    // Push record (producer - hot path, must be fast)
    // Returns true if successfully queued
    bool push(const MLFeatureRecord& record) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & MASK;
        
        // Check if buffer is full
        if (next == tail_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // Copy record to buffer
        buffer_[head] = record;
        
        // Publish
        head_.store(next, std::memory_order_release);
        return true;
    }
    
    // Pop record (consumer - background thread)
    // Returns true if record was available
    bool pop(MLFeatureRecord& out) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Copy record from buffer
        out = buffer_[tail];
        
        // Advance tail
        tail_.store((tail + 1) & MASK, std::memory_order_release);
        return true;
    }
    
    size_t size() const noexcept {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & MASK;
    }
    
    bool empty() const noexcept { return size() == 0; }
    size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    
private:
    alignas(64) std::array<MLFeatureRecord, CAPACITY> buffer_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) std::atomic<size_t> dropped_{0};
};

// =============================================================================
// ML Feature Logger - Background Thread Writer
// =============================================================================
class MLFeatureLogger {
public:
    static constexpr size_t BUFFER_SIZE = 16384;  // 16K records before drop
    
    explicit MLFeatureLogger(const char* path) noexcept 
        : running_(false)
        , records_written_(0)
        , fp_(nullptr)
    {
        std::strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';
    }
    
    ~MLFeatureLogger() {
        stop();
    }
    
    // Non-copyable
    MLFeatureLogger(const MLFeatureLogger&) = delete;
    MLFeatureLogger& operator=(const MLFeatureLogger&) = delete;
    
    // =========================================================================
    // Lifecycle
    // =========================================================================
    
    bool start() noexcept {
        if (running_.load()) return true;
        
        // Open file for append (binary)
        fp_ = std::fopen(path_, "ab");
        if (!fp_) {
            std::fprintf(stderr, "[MLFeatureLogger] Failed to open %s\n", path_);
            return false;
        }
        
        running_.store(true);
        writer_thread_ = std::thread(&MLFeatureLogger::writerLoop, this);
        
        std::printf("[MLFeatureLogger] Started logging to %s\n", path_);
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        running_.store(false);
        
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        
        // Flush any remaining records
        if (fp_) {
            MLFeatureRecord rec;
            while (buffer_.pop(rec)) {
                std::fwrite(&rec, sizeof(MLFeatureRecord), 1, fp_);
                records_written_++;
            }
            std::fflush(fp_);
            std::fclose(fp_);
            fp_ = nullptr;
        }
        
        std::printf("[MLFeatureLogger] Stopped. Records written: %zu, Dropped: %zu\n",
                    records_written_, buffer_.dropped());
    }
    
    // =========================================================================
    // Hot Path API - MUST BE FAST (<100ns)
    // =========================================================================
    
    // Log a feature record (non-blocking)
    inline bool log(const MLFeatureRecord& record) noexcept {
        return buffer_.push(record);
    }
    
    // Log entry decision (before trade) - simplified for 64-byte record
    inline bool logEntry(
        uint64_t ts, uint32_t symbol_id,
        MLMarketState state, MLTradeIntent intent, MLRegime regime,
        float ofi, float vpin, float conviction, float spread_bps,
        uint16_t min_open, int8_t side, uint8_t strat_id
    ) noexcept {
        MLFeatureRecord rec;
        rec.timestamp_ns = ts;
        rec.symbol_id = symbol_id;
        rec.state = static_cast<uint8_t>(state);
        rec.intent = static_cast<uint8_t>(intent);
        rec.regime = static_cast<uint8_t>(regime);
        rec.side = side;
        rec.ofi = ofi;
        rec.vpin = vpin;
        rec.spread_bps = spread_bps;
        rec.conviction_score = conviction;
        rec.minutes_from_open = min_open;
        rec.strategy_id = strat_id;
        // Outcomes filled on close
        rec.realized_R = 0.0f;
        rec.mfe_R = 0.0f;
        rec.mae_R = 0.0f;
        rec.hold_time_ms = 0;
        
        return buffer_.push(rec);
    }
    
    // Log trade close (with outcomes)
    inline bool logClose(
        uint64_t ts, uint32_t symbol_id,
        MLMarketState state, MLTradeIntent intent, MLRegime regime,
        float ofi, float vpin, float conviction, float spread_bps,
        uint16_t min_open, int8_t side, uint8_t strat_id,
        float realized_R, float mfe_R, float mae_R, uint32_t hold_ms
    ) noexcept {
        MLFeatureRecord rec;
        rec.timestamp_ns = ts;
        rec.symbol_id = symbol_id;
        rec.state = static_cast<uint8_t>(state);
        rec.intent = static_cast<uint8_t>(intent);
        rec.regime = static_cast<uint8_t>(regime);
        rec.side = side;
        rec.ofi = ofi;
        rec.vpin = vpin;
        rec.spread_bps = spread_bps;
        rec.conviction_score = conviction;
        rec.minutes_from_open = min_open;
        rec.strategy_id = strat_id;
        rec.realized_R = realized_R;
        rec.mfe_R = mfe_R;
        rec.mae_R = mae_R;
        rec.hold_time_ms = hold_ms;
        
        return buffer_.push(rec);
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    
    size_t recordsWritten() const noexcept { return records_written_; }
    size_t recordsDropped() const noexcept { return buffer_.dropped(); }
    size_t bufferSize() const noexcept { return buffer_.size(); }
    bool isRunning() const noexcept { return running_.load(); }
    
private:
    void writerLoop() noexcept {
        MLFeatureRecord rec;
        
        while (running_.load(std::memory_order_relaxed)) {
            // Batch write for efficiency
            size_t written = 0;
            while (written < 1000 && buffer_.pop(rec)) {
                std::fwrite(&rec, sizeof(MLFeatureRecord), 1, fp_);
                records_written_++;
                written++;
            }
            
            if (written > 0) {
                // Flush periodically
                if (records_written_ % 10000 == 0) {
                    std::fflush(fp_);
                }
            } else {
                // No records - sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
private:
    FeatureRingBuffer<BUFFER_SIZE> buffer_;
    std::atomic<bool> running_;
    std::thread writer_thread_;
    size_t records_written_;
    FILE* fp_;
    char path_[256];
};

// =============================================================================
// CSV Exporter - For offline analysis (cold path)
// =============================================================================
class MLFeatureExporter {
public:
    static bool exportToCSV(const char* binary_path, const char* csv_path) noexcept {
        FILE* fin = std::fopen(binary_path, "rb");
        if (!fin) {
            std::fprintf(stderr, "[MLFeatureExporter] Cannot open %s\n", binary_path);
            return false;
        }
        
        FILE* fout = std::fopen(csv_path, "w");
        if (!fout) {
            std::fclose(fin);
            std::fprintf(stderr, "[MLFeatureExporter] Cannot create %s\n", csv_path);
            return false;
        }
        
        // Header (matches simplified 64-byte record)
        std::fprintf(fout, "timestamp_ns,symbol_id,state,intent,regime,side,"
                           "ofi,vpin,spread_bps,conviction,"
                           "min_open,strategy_id,realized_R,mfe_R,mae_R,hold_ms\n");
        
        MLFeatureRecord rec;
        size_t count = 0;
        
        while (std::fread(&rec, sizeof(MLFeatureRecord), 1, fin) == 1) {
            std::fprintf(fout, 
                "%lu,%u,%u,%u,%u,%d,"
                "%.6f,%.6f,%.6f,%.6f,"
                "%u,%u,%.6f,%.6f,%.6f,%u\n",
                (unsigned long)rec.timestamp_ns, rec.symbol_id,
                (unsigned)rec.state, (unsigned)rec.intent, (unsigned)rec.regime,
                (int)rec.side,
                rec.ofi, rec.vpin, rec.spread_bps, rec.conviction_score,
                (unsigned)rec.minutes_from_open, (unsigned)rec.strategy_id,
                rec.realized_R, rec.mfe_R, rec.mae_R, rec.hold_time_ms
            );
            count++;
        }
        
        std::fclose(fin);
        std::fclose(fout);
        
        std::printf("[MLFeatureExporter] Exported %zu records to %s\n", count, csv_path);
        return true;
    }
};

} // namespace ML
} // namespace Chimera
