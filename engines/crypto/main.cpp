#include <iostream>
#include <thread>
#include <random>
#include <chrono>
#include "chimera/IPC.hpp"

int main() {
    std::string name = "./chimera_" + std::string(
        #ifdef CRYPTO
        "crypto"
        #elif defined(GOLD)
        "gold"
        #else
        "indices"
        #endif
    );

    int fd = ipc_connect();
    double pnl = 0.0;
    std::default_random_engine rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(-5.0, 5.0);

    while (true) {
        pnl += dist(rng);
        ipc_send(fd, name + ":" + std::to_string(pnl));
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
