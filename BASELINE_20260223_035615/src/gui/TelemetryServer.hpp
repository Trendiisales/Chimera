#pragma once
#include <atomic>
#include <thread>

namespace chimera {

class TelemetryServer {
public:
    TelemetryServer();
    ~TelemetryServer();
    void start(int port = 7777);
    void stop();

private:
    void run(int port);
    std::atomic<bool> running_;
    int server_fd_;
    std::thread thread_;
};

}
