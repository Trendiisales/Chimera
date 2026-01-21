#ifndef BCHS_LOGGER_HPP
#define BCHS_LOGGER_HPP

#include "../core/system_state.hpp"
#include "../exec/binance_executor.hpp"
#include <string>
#include <fstream>
#include <mutex>
#include <iomanip>
#include <chrono>

class BCHSLogger {
public:
    explicit BCHSLogger(const std::string& path) : path_(path) {
        file_.open(path, std::ios::out | std::ios::app);
        if (file_.is_open()) {
            file_ << "timestamp,event_type,symbol,side,size,price,strength,"
                  << "depth_ratio,ofi_zscore,ofi_accel,forced_flow,equity,pnl\n";
            file_.flush();
        }
    }

    ~BCHSLogger() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    void logCascade(
        uint64_t ts_ns,
        const std::string& symbol,
        Side side,
        double strength,
        double depth_ratio,
        double ofi_zscore,
        double ofi_accel,
        bool forced_flow,
        double equity
    ) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!file_.is_open()) return;
        
        file_ << ts_ns << ",CASCADE," << symbol << "," << sideStr(side) << ","
              << "0,0," << std::fixed << std::setprecision(4) << strength << ","
              << depth_ratio << "," << ofi_zscore << "," << ofi_accel << ","
              << (forced_flow ? "1" : "0") << "," << equity << ",0\n";
        file_.flush();
    }

    void logFill(const Fill& f, double equity) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!file_.is_open()) return;
        
        std::string type = f.is_shadow ? "SHADOW_FILL" : "LIVE_FILL";
        
        file_ << f.ts_ns << "," << type << "," << f.symbol << "," 
              << sideStr(f.side) << "," << std::fixed << std::setprecision(6) 
              << f.size << "," << std::setprecision(2) << f.price << ","
              << "0,0,0,0,0," << equity << ",0\n";
        file_.flush();
    }

    void logPnL(
        uint64_t ts_ns,
        const std::string& symbol,
        double pnl,
        double equity
    ) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!file_.is_open()) return;
        
        file_ << ts_ns << ",PNL," << symbol << ",NONE,0,0,0,0,0,0,0,"
              << std::fixed << std::setprecision(2) << equity << "," << pnl << "\n";
        file_.flush();
    }

    void logBlock(
        uint64_t ts_ns,
        const std::string& blocker,
        const std::string& blocked,
        uint64_t duration_ns
    ) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!file_.is_open()) return;
        
        file_ << ts_ns << ",BLOCK," << blocker << "->" << blocked 
              << ",NONE,0,0,0,0,0,0,0,0," << duration_ns << "\n";
        file_.flush();
    }

    void logState(
        uint64_t ts_ns,
        const std::string& state,
        double depth_ratio,
        double ofi_zscore,
        double ofi_accel,
        bool forced_flow
    ) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!file_.is_open()) return;
        
        file_ << ts_ns << ",STATE," << state << ",NONE,0,0,0,"
              << std::fixed << std::setprecision(4)
              << depth_ratio << "," << ofi_zscore << "," << ofi_accel << ","
              << (forced_flow ? "1" : "0") << ",0,0\n";
        file_.flush();
    }

private:
    std::string path_;
    std::ofstream file_;
    std::mutex mtx_;
};

#endif
