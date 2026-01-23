#ifndef SYSTEM_STATE_HPP
#define SYSTEM_STATE_HPP

#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <cstdint>

enum class Side { BUY, SELL, NONE };

inline const char* sideStr(Side s) {
    switch (s) {
        case Side::BUY: return "BUY";
        case Side::SELL: return "SELL";
        default: return "NONE";
    }
}

struct CascadeMetrics {
    std::atomic<double> depth_ratio{1.0};
    std::atomic<double> spread_bps{0.0};
    std::atomic<double> ofi_zscore{0.0};
    std::atomic<double> ofi_accel{0.0};
    std::atomic<bool> forced_flow{false};
    std::atomic<bool> impulse_open{false};
    std::atomic<bool> liq_spike{false};
    std::atomic<double> liq_intensity{0.0};
    std::atomic<double> replenish_rate{0.0};
};

struct SymbolState {
    std::atomic<double> last_price{0.0};
    std::atomic<double> bid{0.0};
    std::atomic<double> ask{0.0};
    std::atomic<double> spread_bps{0.0};
    std::atomic<uint64_t> last_ts_ns{0};
};

struct PortfolioState {
    std::atomic<double> equity{0.0};
    std::atomic<double> peak{0.0};
    std::atomic<double> drawdown{0.0};
};

class SystemState {
public:
    std::atomic<uint64_t> ts_ns{0};
    
    CascadeMetrics btc;
    PortfolioState portfolio;
    
    SymbolState& symbol(const std::string& sym) {
        std::lock_guard<std::mutex> lock(mtx_);
        return symbols_[sym];
    }
    
    double getPrice(const std::string& sym) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = symbols_.find(sym);
        if (it != symbols_.end())
            return it->second.last_price.load();
        return 0.0;
    }

private:
    std::unordered_map<std::string, SymbolState> symbols_;
    std::mutex mtx_;
};

struct CascadeEvent {
    Side side = Side::NONE;
    uint64_t ts_ns = 0;
    double strength = 0.0;
    double depth_ratio = 0.0;
    double ofi_zscore = 0.0;
    double ofi_accel = 0.0;
    bool forced_flow = false;
};

#endif
