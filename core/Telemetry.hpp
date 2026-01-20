#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

struct TelemetrySymbol {
    std::string symbol;
    std::string engine;
    std::string regime;
    double edge = 0;
    double expectancy = 0;
    double alloc = 0;
    double net = 0;
    double latency_ms = 0;
    double spread = 0;
    double ofi = 0;
};

struct TelemetryTrade {
    std::string time;
    std::string symbol;
    std::string engine;
    std::string side;
    double qty = 0;
    double entry = 0;
    double exit = 0;
    double pnl = 0;
    double latency_ms = 0;
    std::string reason;
};

struct TelemetryFrame {
    uint64_t seq = 0;
    std::string mode = "SHADOW";

    std::vector<TelemetrySymbol> symbols;
    std::vector<TelemetryTrade> trades;

    double risk_scale = 0;
    bool kill = false;
    double daily_pnl = 0;
};

class TelemetryBus {
public:
    static TelemetryBus& instance() {
        static TelemetryBus t;
        return t;
    }

    void updateSymbol(const TelemetrySymbol& s) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& x : frame_.symbols) {
            if (x.symbol == s.symbol) {
                x = s;
                return;
            }
        }
        frame_.symbols.push_back(s);
    }

    void pushTrade(const TelemetryTrade& t) {
        std::lock_guard<std::mutex> lock(mtx_);
        frame_.trades.insert(frame_.trades.begin(), t);
        if (frame_.trades.size() > 50)
            frame_.trades.resize(50);
    }

    TelemetryFrame snapshot() {
        std::lock_guard<std::mutex> lock(mtx_);
        frame_.seq++;
        return frame_;
    }

    void setRisk(double scale, bool kill, double pnl) {
        std::lock_guard<std::mutex> lock(mtx_);
        frame_.risk_scale = scale;
        frame_.kill = kill;
        frame_.daily_pnl = pnl;
    }

private:
    std::mutex mtx_;
    TelemetryFrame frame_;
};
