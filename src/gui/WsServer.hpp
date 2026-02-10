#pragma once
#include <thread>
#include <atomic>
#include <string>

class WsServer {
public:
    explicit WsServer(int port);
    void start();
    void stop();
    void broadcast(const std::string& message);
    
private:
    void acceptLoop();
    void publishLoop();
    int port_;
    std::thread accept_thread_;
    std::thread publish_thread_;
    std::atomic<bool> running_{false};
};
