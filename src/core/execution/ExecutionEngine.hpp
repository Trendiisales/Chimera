#pragma once
#include <string>

#include "execution/ExecPolicy.hpp"
#include "execution/OrderBookView.hpp"
#include "execution/VenueRouter.hpp"

#include "control/ControlPlane.hpp"
#include "state/EventJournal.hpp"

namespace chimera {

class ExecutionEngine {
public:
    ExecutionEngine(ControlPlane& cp,
                    EventJournal& journal);

    void onBook(const std::string& symbol,
                double bid,
                double ask,
                double bid_depth,
                double ask_depth);

    void onIntent(const std::string& engine,
                  const std::string& symbol,
                  double price,
                  double qty,
                  double edge_score,
                  double latency_ns,
                  uint64_t event_id);

private:
    ControlPlane& m_control;
    EventJournal& m_journal;

    ExecPolicyEngine m_policy;
    OrderBookView m_books;
    VenueRouter m_router;
};

}
