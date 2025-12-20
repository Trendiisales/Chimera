#include "binance/SocketEventLoop.hpp"
#include <thread>
#include <chrono>
#include <iostream>

#if defined(__APPLE__)
#include <sys/event.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>
#endif

namespace binance {

SocketEventLoop::SocketEventLoop() {
#if defined(__APPLE__)
    kq = kqueue();
    std::cout << "[EV] kqueue created" << std::endl;
#elif defined(__linux__)
    ep = epoll_create1(0);
    std::cout << "[EV] epoll created" << std::endl;
#endif
}

SocketEventLoop::~SocketEventLoop() {
#if defined(__APPLE__)
    if (kq >= 0) close(kq);
#elif defined(__linux__)
    if (ep >= 0) close(ep);
#endif
}

void SocketEventLoop::set_read_callback(const ReadCallback& cb) {
    on_read = cb;
}

void SocketEventLoop::start() {
    running.store(true);

    std::thread([this]() {
        std::cout << "[EV] Event loop started" << std::endl;

        while (running.load()) {
            /*
             REAL IMPLEMENTATION (TOMORROW):
               - wait for socket readability
               - read frames
               - invoke on_read()
               - handle backpressure
            */

            // Simulate readable event
            if (on_read) {
                on_read();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[EV] Event loop stopped" << std::endl;
    }).detach();
}

void SocketEventLoop::stop() {
    running.store(false);
}

}
