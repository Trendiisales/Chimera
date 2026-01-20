#include "TelemetryBus.hpp"
#include <thread>
#include <chrono>
#include <iostream>

void runTelemetryServer() {
    while (true) {
        auto engines = TelemetryBus::instance().snapshotEngines();
        auto trades  = TelemetryBus::instance().snapshotTrades();

        std::cout << "{\"engines\":[";

        for (size_t i = 0; i < engines.size(); ++i) {
            const auto& e = engines[i];
            std::cout
                << "{\"symbol\":\"" << e.symbol
                << "\",\"net_bps\":" << e.net_bps
                << ",\"dd_bps\":" << e.dd_bps
                << ",\"trades\":" << e.trades
                << ",\"fees\":" << e.fees
                << ",\"alloc\":" << e.alloc
                << ",\"leverage\":" << e.leverage
                << ",\"state\":\"" << e.state << "\"}";
            if (i + 1 < engines.size()) std::cout << ",";
        }

        std::cout << "],\"trades\":[";

        for (size_t i = 0; i < trades.size(); ++i) {
            const auto& t = trades[i];
            std::cout
                << "{\"engine\":\"" << t.engine
                << "\",\"symbol\":\"" << t.symbol
                << "\",\"side\":\"" << t.side
                << "\",\"bps\":" << t.bps
                << ",\"latency_ms\":" << t.latency_ms
                << ",\"leverage\":" << t.leverage
                << "}";
            if (i + 1 < trades.size()) std::cout << ",";
        }

        std::cout << "]}" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}
