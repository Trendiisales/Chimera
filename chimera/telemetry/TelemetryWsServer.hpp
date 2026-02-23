#pragma once
#include <string>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#endif
#include "TelemetrySink.hpp"

namespace chimera {

class TelemetryWsServer final : public TelemetrySink {
public:
    explicit TelemetryWsServer(int port);
    ~TelemetryWsServer();

    void start();
    void stop();

    void publish(const TelemetryEvent& ev) override;

private:
    int m_port;
    int m_server_fd = -1;
    int m_client_fd = -1;

    std::atomic<bool> m_running{false};
    std::thread m_thread;

    void run();
};

}
