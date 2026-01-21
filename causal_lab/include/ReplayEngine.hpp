#pragma once
#include "EventTypes.hpp"
#include <fstream>
#include <functional>

namespace chimera_lab {

class ReplayEngine {
public:
    explicit ReplayEngine(const std::string& file);

    void onSignal(std::function<void(const EventHeader&, const SignalVector&)> cb);
    void onDecision(std::function<void(const EventHeader&, const DecisionPayload&)> cb);
    void onFill(std::function<void(const EventHeader&, const FillPayload&)> cb);

    void run();

private:
    std::ifstream in;

    std::function<void(const EventHeader&, const SignalVector&)> signal_cb;
    std::function<void(const EventHeader&, const DecisionPayload&)> decision_cb;
    std::function<void(const EventHeader&, const FillPayload&)> fill_cb;
};

} // namespace chimera_lab
