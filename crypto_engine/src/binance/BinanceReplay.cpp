#include "binance/BinanceReplay.hpp"

#include "binance/BinanceTypes.hpp"
#include "binance/BinanceDepthParser.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <cstdint>

namespace binance {

BinanceReplay::BinanceReplay(OrderBook& book_, DeltaGate& gate_)
    : book(book_), gate(gate_) {}

bool BinanceReplay::replay_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[REPLAY] Cannot open file: " << path << "\n";
        return false;
    }

    std::string line;
    uint64_t applied = 0;
    uint64_t gaps = 0;

    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        auto delta = BinanceDepthParser::parse(line);
        if (!delta)
            continue;

        DeltaResult result = gate.evaluate(*delta);

        if (result == DeltaResult::DROP_OLD)
            continue;

        if (result == DeltaResult::GAP) {
            ++gaps;
            std::cerr
                << "[REPLAY] GAP detected "
                << "U=" << delta->U
                << " u=" << delta->u
                << "\n";
            break;
        }

        book.apply_delta(*delta);
        ++applied;
    }

    std::cout
        << "[REPLAY] completed "
        << "applied=" << applied
        << " gaps=" << gaps
        << "\n";

    return gaps == 0;
}

} // namespace binance
