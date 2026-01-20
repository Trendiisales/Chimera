#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "../engines/FundingSniper.hpp"
#include "../risk/KillSwitchGovernor.hpp"
#include "../metrics/EngineControlServer.hpp"

class PortfolioGovernor;
class CorrelationGovernor;

struct DecisionResult {
    bool allowed;
    double size;
};

class SymbolLane {
public:
    SymbolLane(
        const std::string& symbol,
        double min_size,
        int prometheus_port,
        PortfolioGovernor* portfolio,
        void* exec_router,
        std::shared_ptr<CorrelationGovernor> corr
    );

    void onTick(const tier3::TickData& t);
    void updateFunding(double rate, uint64_t next_funding_ts_us);
    DecisionResult applyRiskAndRoute(
        const std::string& engine,
        bool is_buy,
        double confidence,
        double price
    );
    void run();

private:
    std::string symbol_;
    double min_size_;
    std::atomic<uint64_t> tick_count_;

    std::unique_ptr<FundingSniper> funding_sniper_;
    std::unique_ptr<KillSwitchGovernor> kill_gov_;
    std::unique_ptr<EngineControlServer> metrics_server_;
};

