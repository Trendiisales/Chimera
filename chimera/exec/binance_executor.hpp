#ifndef BINANCE_EXECUTOR_HPP
#define BINANCE_EXECUTOR_HPP

#include "../core/system_state.hpp"
#include "risk_governor.hpp"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

enum class ExecMode {
    SHADOW,
    LIVE
};

struct Fill {
    std::string symbol;
    Side side;
    double size;
    double price;
    double commission;
    uint64_t ts_ns;
    bool is_shadow;
};

using FillHandler = std::function<void(const Fill&)>;

class BinanceExecutor {
public:
    BinanceExecutor();
    ~BinanceExecutor();

    void start();
    void stop();

    void setMode(ExecMode mode);
    ExecMode mode() const;

    void placeMarket(
        const std::string& symbol,
        Side side,
        double size,
        bool reduce_only,
        double ref_price,
        double spread_bps
    );

    void onFill(FillHandler h);

    RiskGovernor& risk() { return risk_; }

private:
    void shadowFill(
        const std::string& symbol,
        Side side,
        double size,
        double ref_price,
        double spread_bps
    );

    void liveFill(
        const std::string& symbol,
        Side side,
        double size,
        bool reduce_only
    );

    std::string sign(const std::string& query);
    std::string httpPost(const std::string& url, const std::string& body);

    ExecMode mode_ = ExecMode::SHADOW;
    RiskGovernor risk_;
    
    FillHandler fill_handler_;
    std::mutex handler_mtx_;
    
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;
    
    std::atomic<bool> running_{false};
};

#endif
