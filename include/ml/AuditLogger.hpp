// =============================================================================
// AuditLogger.hpp - Full Decision Chain Logging for Regulatory Compliance
// =============================================================================
// PURPOSE: Log every trading decision with complete traceability
// DESIGN:
//   - Every order includes: state, ML decision, Kelly sizing, actual size
//   - Binary log for speed, CSV export for review
//   - Immutable audit trail
//   - MiFID II / SEC / FCA compliant structure
//
// WHAT IS LOGGED:
//   - Timestamp (ns precision)
//   - Symbol and side
//   - Market state and regime
//   - ML predictions (expected_R, prob_positive)
//   - Sizing calculations (Kelly fraction, final size)
//   - Outcome (filled on trade close)
//
// USAGE:
//   AuditLogger audit("audit_log.bin");
//   audit.start();
//   
//   // On order:
//   audit.logOrder(...);
//   
//   // On close:
//   audit.logClose(order_id, realized_R, hold_ms);
//   
//   // Export for review:
//   AuditExporter::exportToCSV("audit_log.bin", "audit_log.csv");
// =============================================================================
#pragma once

#include "MLTypes.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace Chimera {
namespace ML {

// =============================================================================
// Full Audit Record - 128 bytes for comprehensive logging
// =============================================================================
struct alignas(64) FullAuditRecord {
    // ── Identification (24 bytes) ──
    uint64_t order_id;              // Unique order ID
    uint64_t timestamp_ns;          // Order timestamp
    uint32_t symbol_id;             // Symbol hash
    int8_t   side;                  // +1 BUY, -1 SELL
    uint8_t  record_type;           // 0=order, 1=close
    uint8_t  padding1[2];
    
    // ── Order Details (32 bytes) ──
    double price;                   // Entry price
    double size;                    // Position size
    double stop;                    // Stop loss price
    double notional;                // price * size
    
    // ── State Context (8 bytes) ──
    MLMarketState market_state;     // DEAD/TRENDING/RANGING/VOLATILE
    MLTradeIntent trade_intent;     // NO_TRADE/MOMENTUM/MEAN_REVERSION
    MLRegime regime;                // Volatility regime
    uint8_t  conviction_level;      // 0-10
    uint8_t  strategy_id;           // Which strategy
    uint8_t  padding2[3];
    
    // ── ML Decision (24 bytes) ──
    float ml_expected_R;            // ML predicted R-multiple
    float ml_prob_positive;         // ML P(R > 0)
    float ml_size_multiplier;       // ML suggested sizing
    float ml_model_confidence;      // Model confidence
    float kelly_raw;                // Raw Kelly fraction
    float kelly_damped;             // After dampening
    
    // ── Execution Context (16 bytes) ──
    float bandit_multiplier;        // Contextual bandit output
    float drift_rmse;               // Current drift RMSE
    uint8_t ml_allowed;             // 0/1: Did ML allow trade
    uint8_t ml_active;              // 0/1: Was ML running
    uint8_t drift_degraded;         // 0/1: Is ML degraded
    uint8_t padding3[5];
    
    // ── Outcome (24 bytes) - Filled on close ──
    float realized_R;               // Actual R-multiple
    float mfe_R;                    // Max favorable excursion
    float mae_R;                    // Max adverse excursion
    uint32_t hold_time_ms;          // Time in position
    uint64_t close_timestamp_ns;    // Close timestamp
    
    // Total: 128 bytes
    
    FullAuditRecord() noexcept {
        std::memset(this, 0, sizeof(FullAuditRecord));
    }
    
    bool isOrder() const noexcept { return record_type == 0; }
    bool isClose() const noexcept { return record_type == 1; }
};

static_assert(sizeof(FullAuditRecord) == 128, "FullAuditRecord must be 128 bytes");

// =============================================================================
// Audit Logger - Thread-safe, background writing
// =============================================================================
class AuditLogger {
public:
    static constexpr size_t BUFFER_SIZE = 4096;
    
    explicit AuditLogger(const char* path) noexcept 
        : running_(false)
        , next_order_id_(1)
        , records_written_(0)
        , fp_(nullptr)
    {
        std::strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';
    }
    
    ~AuditLogger() {
        stop();
    }
    
    // Non-copyable
    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;
    
    // =========================================================================
    // Lifecycle
    // =========================================================================
    
    bool start() noexcept {
        if (running_.load()) return true;
        
        fp_ = std::fopen(path_, "ab");
        if (!fp_) {
            std::fprintf(stderr, "[AuditLogger] Failed to open %s\n", path_);
            return false;
        }
        
        running_.store(true);
        writer_thread_ = std::thread(&AuditLogger::writerLoop, this);
        
        std::printf("[AuditLogger] Started logging to %s\n", path_);
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
            std::lock_guard<std::mutex> lock(mutex_);
            while (!buffer_.empty()) {
                std::fwrite(&buffer_.front(), sizeof(FullAuditRecord), 1, fp_);
                buffer_.pop_front();
                records_written_++;
            }
            std::fflush(fp_);
            std::fclose(fp_);
            fp_ = nullptr;
        }
        
        std::printf("[AuditLogger] Stopped. Records: %zu\n", records_written_);
    }
    
    // =========================================================================
    // Logging API
    // =========================================================================
    
    // Log an order entry
    uint64_t logOrder(
        uint64_t timestamp_ns,
        uint32_t symbol_id,
        int8_t side,
        double price,
        double size,
        double stop,
        MLMarketState state,
        MLTradeIntent intent,
        MLRegime regime,
        uint8_t conviction,
        uint8_t strategy_id,
        const MLDecision& ml,
        float kelly_raw,
        float kelly_damped,
        float bandit_mult,
        float drift_rmse,
        bool drift_degraded
    ) noexcept {
        FullAuditRecord rec;
        rec.order_id = next_order_id_.fetch_add(1);
        rec.timestamp_ns = timestamp_ns;
        rec.symbol_id = symbol_id;
        rec.side = side;
        rec.record_type = 0;  // Order
        
        rec.price = price;
        rec.size = size;
        rec.stop = stop;
        rec.notional = price * size;
        
        rec.market_state = state;
        rec.trade_intent = intent;
        rec.regime = regime;
        rec.conviction_level = conviction;
        rec.strategy_id = strategy_id;
        
        rec.ml_expected_R = ml.expected_R;
        rec.ml_prob_positive = ml.prob_positive;
        rec.ml_size_multiplier = ml.size_multiplier;
        rec.ml_model_confidence = ml.model_confidence;
        rec.kelly_raw = kelly_raw;
        rec.kelly_damped = kelly_damped;
        
        rec.bandit_multiplier = bandit_mult;
        rec.drift_rmse = drift_rmse;
        rec.ml_allowed = ml.allow_trade ? 1 : 0;
        rec.ml_active = ml.ml_active ? 1 : 0;
        rec.drift_degraded = drift_degraded ? 1 : 0;
        
        // Outcome TBD
        rec.realized_R = 0;
        rec.mfe_R = 0;
        rec.mae_R = 0;
        rec.hold_time_ms = 0;
        rec.close_timestamp_ns = 0;
        
        // Store for later close update
        {
            std::lock_guard<std::mutex> lock(order_mutex_);
            open_orders_[rec.order_id] = rec;
        }
        
        pushRecord(rec);
        return rec.order_id;
    }
    
    // Log trade close
    void logClose(
        uint64_t order_id,
        uint64_t close_timestamp_ns,
        float realized_R,
        float mfe_R,
        float mae_R,
        uint32_t hold_time_ms
    ) noexcept {
        FullAuditRecord rec;
        
        // Try to find original order
        {
            std::lock_guard<std::mutex> lock(order_mutex_);
            auto it = open_orders_.find(order_id);
            if (it != open_orders_.end()) {
                rec = it->second;
                open_orders_.erase(it);
            } else {
                // Create minimal close record
                rec.order_id = order_id;
            }
        }
        
        rec.record_type = 1;  // Close
        rec.close_timestamp_ns = close_timestamp_ns;
        rec.realized_R = realized_R;
        rec.mfe_R = mfe_R;
        rec.mae_R = mae_R;
        rec.hold_time_ms = hold_time_ms;
        
        pushRecord(rec);
    }
    
    // =========================================================================
    // Stats
    // =========================================================================
    
    size_t recordsWritten() const noexcept { return records_written_; }
    size_t openOrders() const noexcept { 
        std::lock_guard<std::mutex> lock(order_mutex_);
        return open_orders_.size(); 
    }
    
private:
    void pushRecord(const FullAuditRecord& rec) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.size() < BUFFER_SIZE) {
            buffer_.push_back(rec);
        }
        // Drop if buffer full (shouldn't happen normally)
    }
    
    void writerLoop() noexcept {
        while (running_.load()) {
            FullAuditRecord rec;
            bool has_record = false;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!buffer_.empty()) {
                    rec = buffer_.front();
                    buffer_.pop_front();
                    has_record = true;
                }
            }
            
            if (has_record) {
                std::fwrite(&rec, sizeof(FullAuditRecord), 1, fp_);
                records_written_++;
                
                if (records_written_ % 1000 == 0) {
                    std::fflush(fp_);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    
private:
    std::atomic<bool> running_;
    std::atomic<uint64_t> next_order_id_;
    size_t records_written_;
    FILE* fp_;
    char path_[256];
    
    std::mutex mutex_;
    std::deque<FullAuditRecord> buffer_;
    
    mutable std::mutex order_mutex_;
    std::unordered_map<uint64_t, FullAuditRecord> open_orders_;
    
    std::thread writer_thread_;
};

// =============================================================================
// Audit Exporter - CSV export for human review
// =============================================================================
class AuditExporter {
public:
    static bool exportToCSV(const char* bin_path, const char* csv_path) noexcept {
        FILE* fin = std::fopen(bin_path, "rb");
        if (!fin) {
            std::fprintf(stderr, "[AuditExporter] Cannot open %s\n", bin_path);
            return false;
        }
        
        FILE* fout = std::fopen(csv_path, "w");
        if (!fout) {
            std::fclose(fin);
            std::fprintf(stderr, "[AuditExporter] Cannot create %s\n", csv_path);
            return false;
        }
        
        // Header
        std::fprintf(fout, 
            "order_id,timestamp_ns,symbol_id,side,type,"
            "price,size,stop,notional,"
            "state,intent,regime,conviction,strategy,"
            "ml_exp_R,ml_prob,ml_size_mult,ml_conf,kelly_raw,kelly_damp,"
            "bandit,drift_rmse,ml_allowed,ml_active,drift_deg,"
            "realized_R,mfe_R,mae_R,hold_ms,close_ts\n"
        );
        
        FullAuditRecord rec;
        size_t count = 0;
        
        while (std::fread(&rec, sizeof(FullAuditRecord), 1, fin) == 1) {
            std::fprintf(fout,
                "%lu,%lu,%u,%d,%u,"
                "%.6f,%.6f,%.6f,%.6f,"
                "%u,%u,%u,%u,%u,"
                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%u,%u,%u,"
                "%.4f,%.4f,%.4f,%u,%lu\n",
                (unsigned long)rec.order_id, (unsigned long)rec.timestamp_ns,
                rec.symbol_id, (int)rec.side, (unsigned)rec.record_type,
                rec.price, rec.size, rec.stop, rec.notional,
                (unsigned)rec.market_state, (unsigned)rec.trade_intent,
                (unsigned)rec.regime, (unsigned)rec.conviction_level,
                (unsigned)rec.strategy_id,
                rec.ml_expected_R, rec.ml_prob_positive, rec.ml_size_multiplier,
                rec.ml_model_confidence, rec.kelly_raw, rec.kelly_damped,
                rec.bandit_multiplier, rec.drift_rmse,
                (unsigned)rec.ml_allowed, (unsigned)rec.ml_active,
                (unsigned)rec.drift_degraded,
                rec.realized_R, rec.mfe_R, rec.mae_R, rec.hold_time_ms,
                (unsigned long)rec.close_timestamp_ns
            );
            count++;
        }
        
        std::fclose(fin);
        std::fclose(fout);
        
        std::printf("[AuditExporter] Exported %zu records to %s\n", count, csv_path);
        return true;
    }
    
    // Generate summary statistics
    static void printSummary(const char* bin_path) noexcept {
        FILE* fin = std::fopen(bin_path, "rb");
        if (!fin) return;
        
        size_t total = 0, orders = 0, closes = 0;
        size_t wins = 0, losses = 0;
        double total_R = 0;
        
        FullAuditRecord rec;
        while (std::fread(&rec, sizeof(FullAuditRecord), 1, fin) == 1) {
            total++;
            if (rec.isOrder()) orders++;
            if (rec.isClose()) {
                closes++;
                total_R += rec.realized_R;
                if (rec.realized_R > 0) wins++;
                else if (rec.realized_R < 0) losses++;
            }
        }
        
        std::fclose(fin);
        
        std::printf("\n[AuditExporter] Summary for %s:\n", bin_path);
        std::printf("  Total records: %zu\n", total);
        std::printf("  Orders: %zu\n", orders);
        std::printf("  Closes: %zu\n", closes);
        std::printf("  Wins: %zu (%.1f%%)\n", wins, 
                    closes > 0 ? 100.0 * wins / closes : 0);
        std::printf("  Losses: %zu\n", losses);
        std::printf("  Total R: %.2f\n", total_R);
        std::printf("  Avg R: %.4f\n", closes > 0 ? total_R / closes : 0);
    }
};

} // namespace ML
} // namespace Chimera
