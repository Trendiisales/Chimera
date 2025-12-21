#include "cfd/CfdEngine.hpp"

#include <chrono>
#include <iostream>

namespace cfd {

CfdEngine::CfdEngine() {
    state_.store(EngineState::INIT);
    kill_reason_.store(KillReason::NONE);
}

CfdEngine::~CfdEngine() {
    stop(KillReason::NONE);
}

void CfdEngine::set_pnl_callback(PnlCallback cb) {
    pnl_cb_ = std::move(cb);
}

void CfdEngine::emit_pnl(const std::string& tag, double delta_nzd) {
    if (pnl_cb_) {
        pnl_cb_(tag, delta_nzd);
    }
}

void CfdEngine::start() {
    if (running.exchange(true)) return;
    state_.store(EngineState::RUNNING);
    worker = std::thread(&CfdEngine::run, this);
}

void CfdEngine::stop(KillReason reason) {
    if (!running.exchange(false)) return;
    kill_reason_.store(reason);
    state_.store(EngineState::STOPPING);
    if (worker.joinable()) worker.join();
    state_.store(EngineState::DEAD);
}

bool CfdEngine::alive() const {
    return is_alive.load();
}

EngineState CfdEngine::state() const {
    return state_.load();
}

KillReason CfdEngine::kill_reason() const {
    return kill_reason_.load();
}

void CfdEngine::run() {
    is_alive.store(true);
    std::cout << "[CFD] engine up\n";

    while (running.load()) {
        // Placeholder: real fills will call emit_pnl(...)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    is_alive.store(false);
    std::cout << "[CFD] engine down\n";
}

const char* to_string(EngineState s) {
    switch (s) {
        case EngineState::INIT: return "INIT";
        case EngineState::RUNNING: return "RUNNING";
        case EngineState::STOPPING: return "STOPPING";
        case EngineState::DEAD: return "DEAD";
        default: return "UNKNOWN";
    }
}

const char* to_string(KillReason r) {
    switch (r) {
        case KillReason::NONE: return "NONE";
        case KillReason::SOFT_SIGINT: return "SIGINT";
        case KillReason::SOFT_SIGTERM: return "SIGTERM";
        case KillReason::HARD_TIMEOUT: return "HARD_TIMEOUT";
        case KillReason::RISK_LIMIT: return "DAILY_LOSS_LIMIT";
        default: return "UNKNOWN";
    }
}

} // namespace cfd
