#pragma once
#include <atomic>
#include <cstdint>

struct GuiState {
    double bid_xau = 0;
    double ask_xau = 0;
    double bid_xag = 0;
    double ask_xag = 0;

    double spread_xau = 0;
    double spread_xag = 0;

    double pnl = 0;
    uint64_t trades = 0;
    uint64_t rejects = 0;

    double fix_rtt_ms = 0;

    bool connected = false;
    bool shadow = true;

    uint64_t uptime = 0;
};

extern GuiState g_gui;
extern std::atomic<uint64_t> g_gui_seq;
