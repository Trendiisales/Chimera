#pragma once
#include <thread>

class LiveOperatorServer {
public:
    explicit LiveOperatorServer(int port = 8080);
    void start();
    void stop();

private:
    int port_;
    std::thread server_thread_;
};
