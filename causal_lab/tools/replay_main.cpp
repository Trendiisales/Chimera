#include "ReplayEngine.hpp"
#include <iostream>

using namespace chimera_lab;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: chimera_replay <event_log.bin>\n";
        return 1;
    }

    ReplayEngine replay(argv[1]);

    double pnl = 0.0;

    replay.onSignal([](const EventHeader& h, const SignalVector& s) {
        std::cout << "[SIGNAL] id=" << h.event_id
                  << " ofi=" << s.ofi
                  << " impulse=" << s.impulse
                  << " funding=" << s.funding
                  << "\n";
    });

    replay.onDecision([](const EventHeader& h, const DecisionPayload& d) {
        std::cout << "[DECISION] id=" << h.event_id
                  << " trade=" << d.trade
                  << " qty=" << d.qty
                  << " price=" << d.price
                  << "\n";
    });

    replay.onFill([&](const EventHeader& h, const FillPayload& f) {
        double trade_pnl = (f.fill_qty * f.fill_price) - (f.fee_bps * 0.0001 * f.fill_qty * f.fill_price);
        pnl += trade_pnl;
        std::cout << "[FILL] id=" << h.event_id
                  << " pnl=" << trade_pnl
                  << " total=" << pnl
                  << "\n";
    });

    replay.run();

    std::cout << "FINAL PNL: " << pnl << "\n";
    return 0;
}
