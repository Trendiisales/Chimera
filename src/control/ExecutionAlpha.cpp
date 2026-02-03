#include "control/ExecutionAlpha.hpp"

namespace chimera {

ExecutionAlpha::ExecutionAlpha() {
    for (int i = 0; i < 16; ++i)
        m_stats[i] = ExecStats();
}

void ExecutionAlpha::on_fill(int sid, bool maker, double slip_bps, double fill_ms) {
    if (sid < 0 || sid >= 16) return;
    
    ExecStats& s = m_stats[sid];
    s.samples++;

    double k = 0.1;

    if (maker) {
        s.maker_fill_prob = (1.0 - k) * s.maker_fill_prob + k * 1.0;
        s.avg_fill_ms = (1.0 - k) * s.avg_fill_ms + k * fill_ms;
    } else {
        s.maker_fill_prob = (1.0 - k) * s.maker_fill_prob;
        s.taker_slip_bps = (1.0 - k) * s.taker_slip_bps + k * slip_bps;
    }
}

ExecMode ExecutionAlpha::decide(int sid, double edge_bps) {
    if (sid < 0 || sid >= 16) return ExecMode::SKIP;
    
    ExecStats& s = m_stats[sid];

    if (edge_bps < MIN_EDGE)
        return ExecMode::SKIP;

    double maker_ev = edge_bps * s.maker_fill_prob;
    double taker_ev = edge_bps - s.taker_slip_bps;

    if (maker_ev > taker_ev && s.maker_fill_prob > 0.2)
        return ExecMode::POST;

    if (taker_ev > 0.0)
        return ExecMode::CROSS;

    return ExecMode::SKIP;
}

const char* ExecutionAlpha::name(ExecMode m) const {
    switch (m) {
        case ExecMode::POST: return "POST";
        case ExecMode::CROSS: return "CROSS";
        case ExecMode::SKIP: return "SKIP";
        default: return "UNKNOWN";
    }
}

}
