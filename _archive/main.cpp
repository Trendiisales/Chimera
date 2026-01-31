#include "core/spine.hpp"
#include "engines/BTCascade.hpp"
#include "engines/ETHSniper.hpp"
#include "engines/MeanReversion.hpp"
#include "gui/GUIServer.cpp"
#include <thread>
#include <chrono>

using namespace chimera;

int main() {
    Spine spine;
    BTCascade btc;
    ETHSniper eth;
    MeanReversion mean;

    spine.registerEngine(&btc);
    spine.registerEngine(&eth);
    spine.registerEngine(&mean);

    std::thread gui_thread(run_gui, &spine.telemetry());

    uint64_t ts = 0;
    while (true) {
        MarketTick t1;
        t1.symbol = "BTCUSDT";
        t1.bid = 700.0 + (ts % 10);
        t1.ask = t1.bid + 0.5;
        t1.bid_size = 1.0;
        t1.ask_size = 1.0;
        t1.ts_ns = ts;

        MarketTick t2 = t1;
        t2.symbol = "ETHUSDT";
        t2.bid = 750.0 + (ts % 7);
        t2.ask = t2.bid + 0.5;

        spine.onTick(t1);
        spine.onTick(t2);

        ts++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    gui_thread.join();
    return 0;
}
