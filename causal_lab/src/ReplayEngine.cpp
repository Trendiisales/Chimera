#include "ReplayEngine.hpp"
#include <iostream>

namespace chimera_lab {

ReplayEngine::ReplayEngine(const std::string& file) {
    in.open(file, std::ios::binary);
}

void ReplayEngine::onSignal(std::function<void(const EventHeader&, const SignalVector&)> cb) {
    signal_cb = cb;
}

void ReplayEngine::onDecision(std::function<void(const EventHeader&, const DecisionPayload&)> cb) {
    decision_cb = cb;
}

void ReplayEngine::onFill(std::function<void(const EventHeader&, const FillPayload&)> cb) {
    fill_cb = cb;
}

void ReplayEngine::run() {
    while (true) {
        EventHeader hdr{};
        in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!in.good()) break;

        if (hdr.payload_size == 0) continue;

        if (hdr.type == EventType::SIGNAL) {
            SignalVector vec{};
            in.read(reinterpret_cast<char*>(&vec), sizeof(vec));
            if (signal_cb) signal_cb(hdr, vec);
        }
        else if (hdr.type == EventType::DECISION) {
            DecisionPayload dec{};
            in.read(reinterpret_cast<char*>(&dec), sizeof(dec));
            if (decision_cb) decision_cb(hdr, dec);
        }
        else if (hdr.type == EventType::FILL) {
            FillPayload fill{};
            in.read(reinterpret_cast<char*>(&fill), sizeof(fill));
            if (fill_cb) fill_cb(hdr, fill);
        }
        else {
            in.seekg(hdr.payload_size, std::ios::cur);
        }
    }
}

} // namespace chimera_lab
