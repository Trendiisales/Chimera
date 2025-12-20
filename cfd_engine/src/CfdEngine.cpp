#include "cfd/CfdEngine.hpp"
#include <iostream>
#include <chrono>

namespace cfd {

CfdEngine::CfdEngine() {}
CfdEngine::~CfdEngine() { stop(); }

void CfdEngine::start() {
    if (running.exchange(true)) return;
    worker = std::thread(&CfdEngine::run, this);
}

void CfdEngine::stop() {
    running = false;
    if (worker.joinable())
        worker.join();
}

void CfdEngine::run() {
    std::cout << "[CFD] engine up\n";
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "[CFD] engine down\n";
}

}
