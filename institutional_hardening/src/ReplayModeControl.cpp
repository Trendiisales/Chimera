#include "ReplayModeControl.hpp"

namespace chimera {
namespace hardening {

std::atomic<ExecutionMode> ReplayModeControl::mode_{ExecutionMode::LIVE};

void ReplayModeControl::setMode(ExecutionMode m) {
    mode_.store(m);
}

ExecutionMode ReplayModeControl::getMode() {
    return mode_.load();
}

bool ReplayModeControl::isReplay() {
    return mode_.load() == ExecutionMode::REPLAY;
}

}} // namespace chimera::hardening
