#pragma once
#include <atomic>
#include <thread>

namespace cfd {

class CfdEngine {
public:
    CfdEngine();
    ~CfdEngine();

    void start();
    void stop();

private:
    void run();

    std::atomic<bool> running{false};
    std::thread worker;
};

}
