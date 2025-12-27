// ═══════════════════════════════════════════════════════════════════════════════
// Alpha Trading System - ML Data Logger
// ═══════════════════════════════════════════════════════════════════════════════
// VERSION: 1.2.0
// PURPOSE: Log ML features and trade outcomes for model training
//
// OUTPUT FILES:
// - alpha_ml_features_YYYYMMDD.csv   (tick-level features)
// - alpha_ml_trades_YYYYMMDD.csv     (trade outcomes with labels)
// - alpha_ml_sessions_YYYYMMDD.csv   (session-level aggregates)
//
// LOGGING MODES:
// - TICK: Every N ticks (configurable)
// - SIGNAL: On every signal generation
// - TRADE: On trade entry/exit with outcomes
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "MLFeatures.hpp"
#include "../core/Types.hpp"
#include "../exit/ExitLogic.hpp"
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <deque>
#include <atomic>
#include <thread>

namespace Alpha {
namespace ML {

// ═══════════════════════════════════════════════════════════════════════════════
// TRADE OUTCOME RECORD
// ═══════════════════════════════════════════════════════════════════════════════
struct TradeOutcome {
    uint64_t entry_ts = 0;
    uint64_t exit_ts = 0;
    Instrument instrument = Instrument::INVALID;
    int side = 0;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double size = 0.0;
    
    // Entry conditions
    double entry_edge = 0.0;
    double entry_spread = 0.0;
    double entry_atr_ratio = 0.0;
    SessionType entry_session = SessionType::OFF;
    std::string entry_regime;
    std::string entry_signal;
    
    // Exit conditions
    std::string exit_reason;
    double exit_r = 0.0;
    bool moved_to_be = false;
    bool scaled = false;
    int stop_moves = 0;
    
    // Outcome metrics
    double pnl_bps = 0.0;
    double r_multiple = 0.0;
    uint64_t hold_ms = 0;
    double max_adverse_r = 0.0;
    double max_favorable_r = 0.0;
    double max_favorable_bps = 0.0;
    
    // ML features at entry
    FeatureVector entry_features;
    
    [[nodiscard]] std::string to_csv() const noexcept {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6);
        ss << entry_ts << "," << exit_ts << "," << instrument_str(instrument);
        ss << "," << side << "," << entry_price << "," << exit_price << "," << size;
        ss << "," << entry_edge << "," << entry_spread << "," << entry_atr_ratio;
        ss << "," << session_type_str(entry_session) << "," << entry_regime << "," << entry_signal;
        ss << "," << exit_reason << "," << exit_r << "," << (moved_to_be ? 1 : 0);
        ss << "," << (scaled ? 1 : 0) << "," << stop_moves;
        ss << "," << std::setprecision(2) << pnl_bps << "," << r_multiple;
        ss << "," << hold_ms << "," << max_adverse_r << "," << max_favorable_r;
        ss << "," << max_favorable_bps;
        
        // Append entry feature vector
        for (size_t i = 0; i < TOTAL_FEATURES; ++i) {
            ss << "," << std::setprecision(6) << entry_features.features[i];
        }
        
        return ss.str();
    }
    
    static std::string csv_header() noexcept {
        std::ostringstream ss;
        ss << "entry_ts,exit_ts,instrument,side,entry_price,exit_price,size";
        ss << ",entry_edge,entry_spread,entry_atr_ratio";
        ss << ",entry_session,entry_regime,entry_signal";
        ss << ",exit_reason,exit_r,moved_to_be,scaled,stop_moves";
        ss << ",pnl_bps,r_multiple,hold_ms,max_adverse_r,max_favorable_r,max_favorable_bps";
        
        // Feature names (abbreviated)
        ss << ",f_mom_fast,f_mom_slow,f_mom_delta,f_mom_accel";
        ss << ",f_pz5,f_pz20,f_pz100";
        ss << ",f_r1,f_r5,f_r20,f_r100,f_r500";
        ss << ",f_atr_ratio,f_atr_raw,f_vol,f_vol_z,f_vol_reg,f_vol_trend";
        ss << ",f_spread,f_spread_z,f_spread_pct,f_spread_reg";
        ss << ",f_flow,f_intensity,f_flow_z,f_tick_dir,f_up_pct,f_dn_pct";
        ss << ",f_reg_trend,f_reg_range,f_reg_vol,f_reg_quiet,f_reg_trans";
        ss << ",f_sess_asia,f_sess_lon,f_sess_ny,f_sess_off";
        ss << ",f_has_pos,f_pos_side,f_pos_r,f_pos_hold,f_pos_be,f_pos_scaled,f_edge,f_spread";
        
        return ss.str();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SESSION STATISTICS RECORD
// ═══════════════════════════════════════════════════════════════════════════════
struct SessionStats {
    std::string date;
    SessionType session = SessionType::OFF;
    Instrument instrument = Instrument::INVALID;
    
    // Aggregate metrics
    int total_ticks = 0;
    int total_signals = 0;
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    
    double total_pnl_bps = 0.0;
    double total_r = 0.0;
    double max_drawdown_bps = 0.0;
    double avg_hold_ms = 0.0;
    
    double avg_edge = 0.0;
    double avg_spread = 0.0;
    double avg_atr_ratio = 0.0;
    
    [[nodiscard]] std::string to_csv() const noexcept {
        std::ostringstream ss;
        ss << date << "," << session_type_str(session) << "," << instrument_str(instrument);
        ss << "," << total_ticks << "," << total_signals << "," << total_trades;
        ss << "," << winning_trades << "," << losing_trades;
        ss << "," << std::fixed << std::setprecision(2) << total_pnl_bps;
        ss << "," << total_r << "," << max_drawdown_bps << "," << avg_hold_ms;
        ss << "," << std::setprecision(4) << avg_edge << "," << avg_spread << "," << avg_atr_ratio;
        return ss.str();
    }
    
    static std::string csv_header() noexcept {
        return "date,session,instrument,ticks,signals,trades,wins,losses,"
               "pnl_bps,total_r,max_dd,avg_hold_ms,avg_edge,avg_spread,avg_atr";
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ML DATA LOGGER
// ═══════════════════════════════════════════════════════════════════════════════
class MLLogger {
public:
    struct Config {
        bool enabled = true;
        bool log_ticks = true;
        bool log_trades = true;
        bool log_sessions = true;
        int tick_sample_rate = 100;    // Log every N ticks
        std::string output_dir = ".";
    };
    
    explicit MLLogger(const Config& config = Config{}) noexcept 
        : config_(config)
        , running_(false)
    {}
    
    ~MLLogger() {
        stop();
    }
    
    bool start() noexcept {
        if (running_.load()) return false;
        
        // Create date string
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time);
        
        char date_str[16];
        std::strftime(date_str, sizeof(date_str), "%Y%m%d", tm);
        date_string_ = date_str;
        
        // Open log files
        if (config_.log_ticks) {
            std::string path = config_.output_dir + "/alpha_ml_features_" + date_string_ + ".csv";
            feature_file_.open(path, std::ios::app);
            if (feature_file_.is_open() && feature_file_.tellp() == 0) {
                feature_file_ << FeatureVector::csv_header() << "\n";
            }
        }
        
        if (config_.log_trades) {
            std::string path = config_.output_dir + "/alpha_ml_trades_" + date_string_ + ".csv";
            trade_file_.open(path, std::ios::app);
            if (trade_file_.is_open() && trade_file_.tellp() == 0) {
                trade_file_ << TradeOutcome::csv_header() << "\n";
            }
        }
        
        if (config_.log_sessions) {
            std::string path = config_.output_dir + "/alpha_ml_sessions_" + date_string_ + ".csv";
            session_file_.open(path, std::ios::app);
            if (session_file_.is_open() && session_file_.tellp() == 0) {
                session_file_ << SessionStats::csv_header() << "\n";
            }
        }
        
        running_.store(true);
        
        // Start background flush thread
        flush_thread_ = std::thread(&MLLogger::flush_loop, this);
        
        std::cout << "[ML_LOGGER] Started logging to " << config_.output_dir << "\n";
        return true;
    }
    
    void stop() noexcept {
        if (!running_.load()) return;
        
        running_.store(false);
        
        if (flush_thread_.joinable()) {
            flush_thread_.join();
        }
        
        // Final flush and close
        flush_all();
        
        if (feature_file_.is_open()) feature_file_.close();
        if (trade_file_.is_open()) trade_file_.close();
        if (session_file_.is_open()) session_file_.close();
        
        std::cout << "[ML_LOGGER] Stopped. Total logged: "
                  << features_logged_.load() << " features, "
                  << trades_logged_.load() << " trades\n";
    }
    
    void log_features(const FeatureVector& fv) noexcept {
        if (!config_.enabled || !config_.log_ticks || !running_.load()) return;
        
        // Sample rate check
        if (++sample_counter_ % config_.tick_sample_rate != 0) return;
        
        std::lock_guard<std::mutex> lock(feature_mutex_);
        feature_queue_.push_back(fv.to_csv());
        ++features_logged_;
    }
    
    void log_trade(const TradeOutcome& trade) noexcept {
        if (!config_.enabled || !config_.log_trades || !running_.load()) return;
        
        std::lock_guard<std::mutex> lock(trade_mutex_);
        trade_queue_.push_back(trade.to_csv());
        ++trades_logged_;
    }
    
    void log_session(const SessionStats& stats) noexcept {
        if (!config_.enabled || !config_.log_sessions || !running_.load()) return;
        
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_queue_.push_back(stats.to_csv());
    }
    
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] uint64_t features_logged() const noexcept { return features_logged_.load(); }
    [[nodiscard]] uint64_t trades_logged() const noexcept { return trades_logged_.load(); }
    
    Config& config() noexcept { return config_; }

private:
    void flush_loop() noexcept {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            flush_all();
        }
    }
    
    void flush_all() noexcept {
        // Flush features
        {
            std::lock_guard<std::mutex> lock(feature_mutex_);
            if (feature_file_.is_open()) {
                for (const auto& line : feature_queue_) {
                    feature_file_ << line << "\n";
                }
                feature_file_.flush();
            }
            feature_queue_.clear();
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
        
        // Flush sessions
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            if (session_file_.is_open()) {
                for (const auto& line : session_queue_) {
                    session_file_ << line << "\n";
                }
                session_file_.flush();
            }
            session_queue_.clear();
        }
    }
    
    Config config_;
    std::atomic<bool> running_;
    std::string date_string_;
    
    // Output files
    std::ofstream feature_file_;
    std::ofstream trade_file_;
    std::ofstream session_file_;
    
    // Queues for async logging
    std::deque<std::string> feature_queue_;
    std::deque<std::string> trade_queue_;
    std::deque<std::string> session_queue_;
    
    std::mutex feature_mutex_;
    std::mutex trade_mutex_;
    std::mutex session_mutex_;
    
    std::thread flush_thread_;
    
    std::atomic<uint64_t> features_logged_{0};
    std::atomic<uint64_t> trades_logged_{0};
    uint64_t sample_counter_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// GLOBAL INSTANCE
// ═══════════════════════════════════════════════════════════════════════════════
inline MLLogger& getMLLogger() noexcept {
    static MLLogger instance;
    return instance;
}

}  // namespace ML
}  // namespace Alpha
