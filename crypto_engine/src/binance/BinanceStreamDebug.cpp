#include "binance/BinanceStreams.hpp"
#include "binance/BinanceSubscribe.hpp"
#include <iostream>

namespace binance {

void debug_print_streams() {
    std::vector<std::string> syms = {"BTCUSDT", "ETHUSDT"};

    std::cout << "[BINANCE] WS URL:\n"
              << build_ws_url(syms) << "\n";

    std::cout << "[BINANCE] SUBSCRIBE:\n"
              << subscribe_frame(syms) << "\n";
}

}
