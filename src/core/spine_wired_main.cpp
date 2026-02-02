#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <string>

#include "state/PositionState.hpp"
#include "state/EventJournal.hpp"
#include "state/ShadowFillEngine.hpp"
#include "state/EquityLogger.hpp"
#include "gui/GuiBroadcaster.hpp"

#include "control/ControlPlane.hpp"
#include "execution/ExecutionEngine.hpp"

using namespace chimera;

static bool g_running = true;

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void handle_sigint(int) {
    g_running = false;
}

struct OrderIntent {
    std::string engine;
    std::string symbol;
    double price;
    double qty;
    double edge;
};

class Spine {
public:
    Spine(PositionState& ps,
          EventJournal& journal)
        : m_positions(ps),
          m_journal(journal),
          m_shadow(ps, journal),
          m_control(ps, journal),
          m_exec(m_control, journal) {}

    void onBook(const std::string& symbol,
                double bid,
                double ask,
                double bid_depth,
                double ask_depth) {
        m_exec.onBook(symbol, bid, ask, bid_depth, ask_depth);
    }

    void onIntent(const OrderIntent& in) {
        uint64_t eid = m_journal.nextEventId();

        m_shadow.onOrderIntent(
            in.symbol,
            in.engine,
            in.price,
            in.qty
        );

        m_exec.onIntent(
            in.engine,
            in.symbol,
            in.price,
            in.qty,
            in.edge,
            m_last_latency_ns,
            eid
        );
    }

    void setLatency(uint64_t ns) {
        m_last_latency_ns = ns;
        m_control.onLatencySample("GLOBAL", (double)ns);
    }

private:
    PositionState& m_positions;
    EventJournal& m_journal;

    ShadowFillEngine m_shadow;
    ControlPlane m_control;
    ExecutionEngine m_exec;

    uint64_t m_last_latency_ns = 0;
};

int main() {
    std::signal(SIGINT, handle_sigint);

    PositionState positions;
    EventJournal journal("runs/journal/live");
    EquityLogger equity("runs/equity.csv", positions);
    GuiBroadcaster gui(positions);

    Spine spine(positions, journal);

    std::cout << "[CHIMERA] WIRED SPINE ONLINE\n";

    double bid = 50000;
    double ask = 50001;

    uint64_t tick = 0;

    while (g_running) {
        spine.onBook("BTCUSDT", bid, ask, 10, 10);

        OrderIntent in;
        in.engine = "ETHSniper";
        in.symbol = "BTCUSDT";
        in.price = ask;
        in.qty = (tick % 2 == 0 ? 0.1 : -0.1);
        in.edge = 1.5;

        spine.setLatency(1000000);
        spine.onIntent(in);

        uint64_t ts = now_ns();
        equity.tick(ts);
        gui.onTick(ts);

        std::cout << "[GUI] " << gui.snapshotJSON() << "\n";

        bid += 5;
        ask += 5;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        tick++;
    }

    std::cout << "[CHIMERA] SHUTDOWN\n";
    return 0;
}
