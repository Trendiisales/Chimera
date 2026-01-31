#!/usr/bin/env bash
set -euo pipefail

BASE="$PWD/src/core/execution"
STATE="$PWD/src/core/state"
LAT="$PWD/src/core/latency"
CTRL="$PWD/src/core/control"

mkdir -p "$BASE"

############################################
# EXEC POLICY
############################################
cat > "$BASE/ExecPolicy.hpp" << 'HPP'
#pragma once
#include <string>
#include <cstdint>

namespace chimera {

enum ExecMode {
    MAKER,
    TAKER
};

struct ExecPolicy {
    ExecMode mode = MAKER;
    double slice_size = 0.0;
    double price_offset = 0.0;
    uint64_t repost_ns = 0;
};

class ExecPolicyEngine {
public:
    ExecPolicyEngine();

    ExecPolicy decide(double edge_score,
                      double latency_ns,
                      double spread_bps,
                      double depth);

private:
    double m_latency_thresh_ns;
    double m_edge_thresh;
};

}
HPP

cat > "$BASE/ExecPolicy.cpp" << 'CPP'
#include "execution/ExecPolicy.hpp"

namespace chimera {

ExecPolicyEngine::ExecPolicyEngine()
    : m_latency_thresh_ns(5000000.0),
      m_edge_thresh(1.2) {}

ExecPolicy ExecPolicyEngine::decide(double edge_score,
                                    double latency_ns,
                                    double spread_bps,
                                    double depth) {
    ExecPolicy p;

    if (edge_score > m_edge_thresh && latency_ns < m_latency_thresh_ns) {
        p.mode = TAKER;
        p.slice_size = depth * 0.25;
        p.price_offset = 0.0;
        p.repost_ns = 0;
    } else {
        p.mode = MAKER;
        p.slice_size = depth * 0.10;
        p.price_offset = spread_bps * 0.5;
        p.repost_ns = 20000000;
    }

    return p;
}

}
CPP

############################################
# ORDER BOOK VIEW
############################################
cat > "$BASE/OrderBookView.hpp" << 'HPP'
#pragma once
#include <string>

namespace chimera {

struct BookTop {
    double bid = 0.0;
    double ask = 0.0;
    double bid_depth = 0.0;
    double ask_depth = 0.0;
};

class OrderBookView {
public:
    void update(const std::string& symbol,
                double bid,
                double ask,
                double bid_depth,
                double ask_depth);

    BookTop top(const std::string& symbol) const;

private:
    struct Entry {
        double bid = 0.0;
        double ask = 0.0;
        double bid_depth = 0.0;
        double ask_depth = 0.0;
    };

    std::unordered_map<std::string, Entry> m_books;
};

}
HPP

cat > "$BASE/OrderBookView.cpp" << 'CPP'
#include "execution/OrderBookView.hpp"
#include <unordered_map>

namespace chimera {

void OrderBookView::update(const std::string& symbol,
                           double bid,
                           double ask,
                           double bid_depth,
                           double ask_depth) {
    auto& e = m_books[symbol];
    e.bid = bid;
    e.ask = ask;
    e.bid_depth = bid_depth;
    e.ask_depth = ask_depth;
}

BookTop OrderBookView::top(const std::string& symbol) const {
    BookTop out;
    auto it = m_books.find(symbol);
    if (it == m_books.end()) return out;

    out.bid = it->second.bid;
    out.ask = it->second.ask;
    out.bid_depth = it->second.bid_depth;
    out.ask_depth = it->second.ask_depth;
    return out;
}

}
CPP

############################################
# VENUE ROUTER
############################################
cat > "$BASE/VenueRouter.hpp" << 'HPP'
#pragma once
#include <string>
#include <cstdint>

#include "state/EventJournal.hpp"

namespace chimera {

class VenueRouter {
public:
    explicit VenueRouter(EventJournal& journal);

    void sendOrder(const std::string& venue,
                   const std::string& symbol,
                   double price,
                   double qty,
                   bool taker,
                   uint64_t event_id);

private:
    EventJournal& m_journal;
};

}
HPP

cat > "$BASE/VenueRouter.cpp" << 'CPP'
#include "execution/VenueRouter.hpp"
#include <sstream>

namespace chimera {

VenueRouter::VenueRouter(EventJournal& journal)
    : m_journal(journal) {}

void VenueRouter::sendOrder(const std::string& venue,
                            const std::string& symbol,
                            double price,
                            double qty,
                            bool taker,
                            uint64_t event_id) {
    std::ostringstream p;
    p << "{"
      << "\"venue\":\"" << venue << "\","
      << "\"symbol\":\"" << symbol << "\","
      << "\"price\":" << price << ","
      << "\"qty\":" << qty << ","
      << "\"mode\":\"" << (taker ? "TAKER" : "MAKER") << "\""
      << "}";

    m_journal.write("ORDER_ROUTED", p.str(), event_id);
}

}
CPP

############################################
# EXECUTION ENGINE
############################################
cat > "$BASE/ExecutionEngine.hpp" << 'HPP'
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
HPP

cat > "$BASE/ExecutionEngine.cpp" << 'CPP'
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
CPP

echo "[CHIMERA] EXECUTION ENGINE INSTALLED"
echo "PATH: src/core/execution"
