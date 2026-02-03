#pragma once
#include <cstdint>

namespace chimera {

struct ExecStats {
    double maker_fill_prob = 0.5;
    double avg_fill_ms = 500.0;
    double taker_slip_bps = 2.0;
    uint64_t samples = 0;
};

enum class ExecMode {
    POST = 0,
    CROSS = 1,
    SKIP = 2
};

class ExecutionAlpha {
public:
    ExecutionAlpha();

    void on_fill(int sid, bool maker, double slip_bps, double fill_ms);
    ExecMode decide(int sid, double expected_edge_bps);

    const char* name(ExecMode m) const;

private:
    ExecStats m_stats[16];

    static constexpr double MIN_EDGE = 3.0;
};

}
