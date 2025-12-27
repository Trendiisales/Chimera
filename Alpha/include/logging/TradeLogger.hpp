// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - Enhanced Trade Logging
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
// PURPOSE: Comprehensive trade logging for XAUUSD and NAS100
//
// LOG FILES:
// - alpha_trades_YYYYMMDD.csv       (detailed trade log)
// - alpha_signals_YYYYMMDD.csv      (all signals, taken and rejected)
// - alpha_ticks_YYYYMMDD.bin        (binary tick data for replay)
// - alpha_events_YYYYMMDD.log       (structured event log)
//
// FEATURES:
// - Async non-blocking logging
// - Automatic daily rotation
// - Binary tick storage for backtesting
// - Signal attribution tracking
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "../core/Types.hpp"
#include "../session/SessionDetector.hpp"
#include "../exit/ExitLogic.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <deque>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>

namespace Alpha {
namespace Logging {

// ═══════════════════════════════════════════════════════════════════════════════
// LOG LEVELS
// ═══════════════════════════════════════════════════════════════════════════════
enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

inline const char* log_level_str(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SIGNAL LOG ENTRY
// ═══════════════════════════════════════════════════════════════════════════════
struct SignalEntry {
    uint64_t timestamp_ns = 0;
    Instrument instrument = Instrument::INVALID;
    Side direction = Side::FLAT;
    double strength = 0.0;
    double edge_bps = 0.0;
    double displacement = 0.0;
    std::string regime;
    std::string reason;
    SessionType session = SessionType::OFF;
    double spread = 0.0;
    double atr_ratio = 0.0;
    bool taken = false;
    std::string reject_reason;
    
    [[nodiscard]] std::string to_csv() const noexcept {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6);
        ss << timestamp_ns << "," << instrument_str(instrument);
        ss << "," << side_str(direction) << "," << strength << "," << edge_bps;
        ss << "," << displacement << "," << regime << "," << reason;
        ss << "," << session_type_str(session) << "," << spread << "," << atr_ratio;
        ss << "," << (taken ? "TAKEN" : "REJECTED") << "," << reject_reason;
        return ss.str();
    }
    
    static std::string csv_header() noexcept {
        return "timestamp_ns,instrument,direction,strength,edge_bps,displacement,"
               "regime,reason,session,spread,atr_ratio,status,reject_reason";
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// TRADE LOG ENTRY
// ═══════════════════════════════════════════════════════════════════════════════
struct TradeEntry {
    uint64_t entry_ts = 0;
    uint64_t exit_ts = 0;
    Instrument instrument = Instrument::INVALID;
    Side side = Side::FLAT;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double size = 0.0;
    double pnl_bps = 0.0;
    double r_multiple = 0.0;
    uint64_t hold_ms = 0;
    
    SessionType entry_session = SessionType::OFF;
    std::string entry_regime;
    double entry_edge = 0.0;
    double entry_spread = 0.0;
    double entry_atr_ratio = 0.0;
    
    std::string exit_reason;
    bool moved_to_be = false;
    bool scaled = false;
    int stop_moves = 0;
    
    double max_favorable_r = 0.0;
    double max_adverse_r = 0.0;
    
    [[nodiscard]] std::string to_csv() const noexcept {
        std::ostringstream ss;
        ss << std::fixed;
        ss << entry_ts << "," << exit_ts << "," << instrument_str(instrument);
        ss << "," << side_str(side) << "," << std::setprecision(5) << entry_price;
        ss << "," << exit_price << "," << std::setprecision(4) << size;
        ss << "," << std::setprecision(2) << pnl_bps << "," << r_multiple;
        ss << "," << hold_ms;
        ss << "," << session_type_str(entry_session) << "," << entry_regime;
        ss << "," << std::setprecision(4) << entry_edge << "," << entry_spread;
        ss << "," << entry_atr_ratio << "," << exit_reason;
        ss << "," << (moved_to_be ? 1 : 0) << "," << (scaled ? 1 : 0) << "," << stop_moves;
        ss << "," << max_favorable_r << "," << max_adverse_r;
        return ss.str();
    }
    
    static std::string csv_header() noexcept {
        return "entry_ts,exit_ts,instrument,side,entry_price,exit_price,size,"
               "pnl_bps,r_multiple,hold_ms,entry_session,entry_regime,"
               "entry_edge,entry_spread,entry_atr_ratio,exit_reason,"
               "moved_to_be,scaled,stop_moves,max_fav_r,max_adv_r";
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// TICK RECORD (for binary logging)
// ═══════════════════════════════════════════════════════════════════════════════
#pragma pack(push, 1)
struct TickRecord {
    uint64_t timestamp_ns;
    uint8_t instrument;
    double bid;
    double ask;
    uint32_t sequence;
};
#pragma pack(pop)

// ═══════════════════════════════════════════════════════════════════════════════
// EVENT LOG ENTRY
// ═══════════════════════════════════════════════════════════════════════════════
struct EventEntry {
    uint64_t timestamp_ns = 0;
    LogLevel level = LogLevel::INFO;
    std::string category;
    std::string message;
    Instrument instrument = Instrument::INVALID;
    
    [[nodiscard]] std::string format() const noexcept {
        std::ostringstream ss;
        
        // Format timestamp
        auto ns = timestamp_ns;
        auto secs = ns / 1'000'000'000ULL;
        auto ms = (ns % 1'000'000'000ULL) / 1'000'000ULL;
        
        auto time = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(time);
        std::tm* tm = std::localtime(&t);
        
        ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms;
        ss << " [" << log_level_str(level) << "]";
        ss << " [" << category << "]";
        if (instrument != Instrument::INVALID) {
            ss << " [" << instrument_str(instrument) << "]";
        }
        ss << " " << message;
        
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// TRADE LOGGER
// ═══════════════════════════════════════════════════════════════════════════════
class TradeLogger {
public:
    struct Config {
        bool enabled = true;
        bool log_signals = true;
        bool log_trades = true;
        bool log_ticks = false;    // Binary tick log (can be large)
        bool log_events = true;
        LogLevel min_level = LogLevel::INFO;
        std::string output_dir = ".";
        bool console_output = true;
    };
    
    explicit TradeLogger(const Config& config = Config{}) noexcept 
        : config_(config)
        , running_(false)
    {}
    
    ~TradeLogger() {
        stop();
    }
    
    bool start() noexcept {
        if (running_.load()) return false;
        
        // Generate date string
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        char date_str[16];
        std::strftime(date_str, sizeof(date_str), "%Y%m%d", tm);
        date_string_ = date_str;
        
        // Open log files
        if (config_.log_signals) {
            std::string path = config_.output_dir + "/alpha_signals_" + date_string_ + ".csv";
            signal_file_.open(path, std::ios::app);
            if (signal_file_.is_open() && signal_file_.tellp() == 0) {
                signal_file_ << SignalEntry::csv_header() << "\n";
            }
        }
        
        if (config_.log_trades) {
            std::string path = config_.output_dir + "/alpha_trades_" + date_string_ + ".csv";
            trade_file_.open(path, std::ios::app);
            if (trade_file_.is_open() && trade_file_.tellp() == 0) {
                trade_file_ << TradeEntry::csv_header() << "\n";
            }
        }
        
        if (config_.log_ticks) {
            std::string path = config_.output_dir + "/alpha_ticks_" + date_string_ + ".bin";
            tick_file_.open(path, std::ios::binary | std::ios::app);
        }
        
        if (config_.log_events) {
            std::string path = config_.output_dir + "/alpha_events_" + date_string_ + ".log";
            event_file_.open(path, std::ios::app);
        }
        
        running_.store(true);
        flush_thread_ = std::thread(&TradeLogger::flush_loop, this);
        
        log(LogLevel::INFO, "SYSTEM", "TradeLogger started", Instrument::INVALID);
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        log(LogLevel::INFO, "SYSTEM", "TradeLogger stopping", Instrument::INVALID);
        
        running_.store(false);
        
        if (flush_thread_.joinable()) {
            flush_thread_.join();
        }
        
        flush_all();
        
        if (signal_file_.is_open()) signal_file_.close();
        if (trade_file_.is_open()) trade_file_.close();
        if (tick_file_.is_open()) tick_file_.close();
        if (event_file_.is_open()) event_file_.close();
        
        std::cout << "[LOGGER] Stopped. Signals: " << signals_logged_.load()
                  << " Trades: " << trades_logged_.load()
                  << " Ticks: " << ticks_logged_.load() << "\n";
    }
    
    void log_signal(const SignalEntry& entry) noexcept {
        if (!config_.enabled || !config_.log_signals || !running_.load()) return;
        
        std::lock_guard<std::mutex> lock(signal_mutex_);
        signal_queue_.push_back(entry.to_csv());
        ++signals_logged_;
    }
    
    void log_trade(const TradeEntry& entry) noexcept {
        if (!config_.enabled || !config_.log_trades || !running_.load()) return;
        
        std::lock_guard<std::mutex> lock(trade_mutex_);
        trade_queue_.push_back(entry.to_csv());
        ++trades_logged_;
        
        // Also log to console
        if (config_.console_output) {
            std::cout << "[TRADE] " << instrument_str(entry.instrument) << " "
                      << side_str(entry.side) << " " << entry.size
                      << " @ " << std::fixed << std::setprecision(2) << entry.entry_price
                      << " -> " << entry.exit_price
                      << " PnL=" << entry.pnl_bps << "bps"
                      << " R=" << entry.r_multiple
                      << " (" << entry.exit_reason << ")\n";
        }
    }
    
    void log_tick(const Tick& tick) noexcept {
        if (!config_.enabled || !config_.log_ticks || !running_.load()) return;
        
        TickRecord rec;
        rec.timestamp_ns = tick.timestamp_ns;
        rec.instrument = static_cast<uint8_t>(tick.instrument);
        rec.bid = tick.bid;
        rec.ask = tick.ask;
        rec.sequence = static_cast<uint32_t>(tick.sequence);
        
        std::lock_guard<std::mutex> lock(tick_mutex_);
        tick_queue_.push_back(rec);
        ++ticks_logged_;
    }
    
    void log(LogLevel level, const std::string& category, const std::string& message,
             Instrument instrument = Instrument::INVALID) noexcept {
        if (!config_.enabled || !config_.log_events || !running_.load()) return;
        if (level < config_.min_level) return;
        
        EventEntry entry;
        entry.timestamp_ns = now_ns();
        entry.level = level;
        entry.category = category;
        entry.message = message;
        entry.instrument = instrument;
        
        std::string formatted = entry.format();
        
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            event_queue_.push_back(formatted);
        }
        
        if (config_.console_output && level >= LogLevel::INFO) {
            std::cout << formatted << "\n";
        }
    }
    
    // Convenience methods
    void trace(const std::string& cat, const std::string& msg, Instrument i = Instrument::INVALID) { log(LogLevel::TRACE, cat, msg, i); }
    void debug(const std::string& cat, const std::string& msg, Instrument i = Instrument::INVALID) { log(LogLevel::DEBUG, cat, msg, i); }
    void info(const std::string& cat, const std::string& msg, Instrument i = Instrument::INVALID) { log(LogLevel::INFO, cat, msg, i); }
    void warn(const std::string& cat, const std::string& msg, Instrument i = Instrument::INVALID) { log(LogLevel::WARN, cat, msg, i); }
    void error(const std::string& cat, const std::string& msg, Instrument i = Instrument::INVALID) { log(LogLevel::ERROR, cat, msg, i); }
    void fatal(const std::string& cat, const std::string& msg, Instrument i = Instrument::INVALID) { log(LogLevel::FATAL, cat, msg, i); }
    
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] uint64_t signals_logged() const noexcept { return signals_logged_.load(); }
    [[nodiscard]] uint64_t trades_logged() const noexcept { return trades_logged_.load(); }
    [[nodiscard]] uint64_t ticks_logged() const noexcept { return ticks_logged_.load(); }
    
    Config& config() noexcept { return config_; }

private:
    void flush_loop() noexcept {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            flush_all();
        }
    }
    
    void flush_all() noexcept {
        // Flush signals
        {
            std::lock_guard<std::mutex> lock(signal_mutex_);
            if (signal_file_.is_open()) {
                for (const auto& line : signal_queue_) {
                    signal_file_ << line << "\n";
                }
                signal_file_.flush();
            }
            signal_queue_.clear();
        }
        
        // Flush trades
        {
            std::lock_guard<std::mutex> lock(trade_mutex_);
            if (trade_file_.is_open()) {
                for (const auto& line : trade_queue_) {
                    trade_file_ << line << "\n";
                }
                trade_file_.flush();
            }
            trade_queue_.clear();
        }
        
        // Flush ticks (binary)
        {
            std::lock_guard<std::mutex> lock(tick_mutex_);
            if (tick_file_.is_open() && !tick_queue_.empty()) {
                tick_file_.write(reinterpret_cast<const char*>(tick_queue_.data()),
                                 tick_queue_.size() * sizeof(TickRecord));
                tick_file_.flush();
            }
            tick_queue_.clear();
        }
        
        // Flush events
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            if (event_file_.is_open()) {
                for (const auto& line : event_queue_) {
                    event_file_ << line << "\n";
                }
                event_file_.flush();
            }
            event_queue_.clear();
        }
    }
    
    Config config_;
    std::atomic<bool> running_;
    std::string date_string_;
    
    // Output files
    std::ofstream signal_file_;
    std::ofstream trade_file_;
    std::ofstream tick_file_;
    std::ofstream event_file_;
    
    // Queues
    std::deque<std::string> signal_queue_;
    std::deque<std::string> trade_queue_;
    std::deque<TickRecord> tick_queue_;
    std::deque<std::string> event_queue_;
    
    std::mutex signal_mutex_;
    std::mutex trade_mutex_;
    std::mutex tick_mutex_;
    std::mutex event_mutex_;
    
    std::thread flush_thread_;
    
    std::atomic<uint64_t> signals_logged_{0};
    std::atomic<uint64_t> trades_logged_{0};
    std::atomic<uint64_t> ticks_logged_{0};
};

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCE
// ═══════════════════════════════════════════════════════════════════════════════
inline TradeLogger& getLogger() noexcept {
    static TradeLogger instance;
    return instance;
}

// Convenience macros
#define ALPHA_LOG_TRACE(cat, msg) Alpha::Logging::getLogger().trace(cat, msg)
#define ALPHA_LOG_DEBUG(cat, msg) Alpha::Logging::getLogger().debug(cat, msg)
#define ALPHA_LOG_INFO(cat, msg)  Alpha::Logging::getLogger().info(cat, msg)
#define ALPHA_LOG_WARN(cat, msg)  Alpha::Logging::getLogger().warn(cat, msg)
#define ALPHA_LOG_ERROR(cat, msg) Alpha::Logging::getLogger().error(cat, msg)

}  // namespace Logging
}  // namespace Alpha
