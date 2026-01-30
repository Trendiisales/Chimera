#include "state/EquityLogger.hpp"
#include "state/PositionState.hpp"
#include <fstream>

namespace chimera {

EquityLogger::EquityLogger(const std::string& path,
                           PositionState& ps)
    : m_path(path),
      m_positions(ps),
      m_last_ts(0) {
    std::ofstream f(m_path, std::ios::out);
    f << "ts_ns,equity\n";
}

void EquityLogger::tick(uint64_t ts_ns) {
    if (ts_ns == m_last_ts) return;
    m_last_ts = ts_ns;

    std::ofstream f(m_path, std::ios::app);
    f << ts_ns << "," << m_positions.totalEquity() << "\n";
}

}
