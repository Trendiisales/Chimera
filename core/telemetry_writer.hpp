#pragma once
#include <fstream>
#include <atomic>
#include "core/contract.hpp"

class TelemetryWriter {
    std::ofstream bin_;
    std::ofstream json_;
    std::atomic<uint64_t> seq_{0};

public:
    TelemetryWriter(const std::string& bin, const std::string& json) {
        bin_.open(bin, std::ios::binary | std::ios::out);
        json_.open(json, std::ios::out);
    }

    void write_tick(const MarketTick& t) {
        uint64_t id = seq_++;
        bin_.write((char*)&id, sizeof(id));
        bin_.write((char*)&t.bid, sizeof(double));
        bin_.write((char*)&t.ask, sizeof(double));
        json_ << "{\"id\":" << id
              << ",\"type\":\"tick\""
              << ",\"sym\":\"" << t.symbol << "\""
              << ",\"bid\":" << t.bid
              << ",\"ask\":" << t.ask
              << "}\n";
        json_.flush();
    }

    void write_intent(const OrderIntent& o) {
        uint64_t id = seq_++;
        json_ << "{\"id\":" << id
              << ",\"type\":\"intent\""
              << ",\"sym\":\"" << o.symbol << "\""
              << ",\"px\":" << o.price
              << ",\"qty\":" << o.qty
              << ",\"edge\":" << o.edge
              << "}\n";
        json_.flush();
    }

    void write_fill(const FillEvent& f) {
        uint64_t id = seq_++;
        json_ << "{\"id\":" << id
              << ",\"type\":\"fill\""
              << ",\"sym\":\"" << f.symbol << "\""
              << ",\"px\":" << f.price
              << ",\"qty\":" << f.qty
              << ",\"pnl_bps\":" << f.pnl_bps
              << "}\n";
        json_.flush();
    }
};
