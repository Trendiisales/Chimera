#include "execution/ExecutionEngine.hpp"
#include <sstream>

namespace chimera {

ExecutionEngine::ExecutionEngine(ControlPlane& cp,
                                 EventJournal& journal)
    : m_control(cp),
      m_journal(journal),
      m_router(journal) {}

void ExecutionEngine::onBook(const std::string& symbol,
                             double bid,
                             double ask,
                             double bid_depth,
                             double ask_depth) {
    m_books.update(symbol, bid, ask, bid_depth, ask_depth);
}

void ExecutionEngine::onIntent(const std::string& engine,
                               const std::string& symbol,
                               double,
                               double qty,
                               double edge_score,
                               double latency_ns,
                               uint64_t event_id) {
    auto decision = m_control.evaluate(
        engine,
        symbol,
        0.0,
        qty,
        event_id
    );

    if (!decision.allowed) {
        std::ostringstream p;
        p << "{"
          << "\"engine\":\"" << engine << "\","
          << "\"symbol\":\"" << symbol << "\","
          << "\"reason\":\"" << decision.reason << "\""
          << "}";
        m_journal.write("EXEC_REJECTED", p.str(), event_id);
        return;
    }

    BookTop top = m_books.top(symbol);
    double spread = (top.ask - top.bid);
    double depth = (qty > 0 ? top.ask_depth : top.bid_depth);

    ExecPolicy pol = m_policy.decide(
        edge_score,
        latency_ns,
        spread,
        depth
    );

    double px = qty > 0 ? top.ask : top.bid;
    if (pol.mode == MAKER) {
        if (qty > 0) px -= pol.price_offset;
        else px += pol.price_offset;
    }

    double final_qty = qty * decision.size_mult;
    double slice = pol.slice_size > 0.0 ? pol.slice_size : final_qty;

    std::ostringstream meta;
    meta << "{"
         << "\"engine\":\"" << engine << "\","
         << "\"symbol\":\"" << symbol << "\","
         << "\"mode\":\"" << (pol.mode == TAKER ? "TAKER" : "MAKER") << "\","
         << "\"slice\":" << slice
         << "}";

    m_journal.write("EXEC_POLICY", meta.str(), event_id);

    m_router.sendOrder(
        "PRIMARY",
        symbol,
        px,
        slice,
        pol.mode == TAKER,
        event_id
    );
}

}
