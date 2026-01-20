#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <fstream>
#include <chrono>

struct TradeRecord {
    std::string symbol;
    std::string bucket;
    std::string engine;
    bool is_buy;
    double qty;
    double entry;
    double exit;
    double pnl;
    double fees;
    double funding;
    uint64_t ts_entry;
    uint64_t ts_exit;
};

class TradeLedger {
public:
    TradeLedger(const std::string& path) : path_(path) {}

    void record(const TradeRecord& t) {
        std::lock_guard<std::mutex> g(mu_);
        trades_.push_back(t);
        flush(t);
    }

    std::vector<TradeRecord> snapshot() {
        std::lock_guard<std::mutex> g(mu_);
        return trades_;
    }

private:
    void flush(const TradeRecord& t) {
        std::ofstream f(path_, std::ios::app);
        f <<
        "{"
        "\"symbol\":\"" << t.symbol << "\","
        "\"bucket\":\"" << t.bucket << "\","
        "\"engine\":\"" << t.engine << "\","
        "\"side\":\"" << (t.is_buy ? "BUY":"SELL") << "\","
        "\"qty\":" << t.qty << ","
        "\"entry\":" << t.entry << ","
        "\"exit\":" << t.exit << ","
        "\"pnl\":" << t.pnl << ","
        "\"fees\":" << t.fees << ","
        "\"funding\":" << t.funding << ","
        "\"ts_entry\":" << t.ts_entry << ","
        "\"ts_exit\":" << t.ts_exit <<
        "}\n";
    }

    std::mutex mu_;
    std::string path_;
    std::vector<TradeRecord> trades_;
};
