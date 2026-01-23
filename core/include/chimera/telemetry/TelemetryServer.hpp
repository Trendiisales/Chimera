#pragma once
#include <atomic>
#include <thread>
#include <string>

namespace chimera {

class TelemetryServer {
public:
    explicit TelemetryServer(int port = 8080);
    ~TelemetryServer();

    void start();
    void stop();

private:
    void run();
    void handle_client(int fd);
    std::string build_json();

    int port_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace chimera
