#include "platform/platform.hpp"
#include "TelemetryWsServer.hpp"

#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#endif

namespace chimera {

TelemetryWsServer::TelemetryWsServer(int port)
    : m_port(port) {}

TelemetryWsServer::~TelemetryWsServer() {
    stop();
}

void TelemetryWsServer::start() {
    m_running = true;
    m_thread = std::thread(&TelemetryWsServer::run, this);
}

void TelemetryWsServer::stop() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    if (m_server_fd >= 0)
        plat::socket_close(m_server_fd);
}

void TelemetryWsServer::run() {
    m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(m_server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(m_server_fd, 1);

    m_client_fd = accept(m_server_fd, nullptr, nullptr);

    while (m_running) {
        plat::sleep_us(1000);
    }
}

void TelemetryWsServer::publish(const TelemetryEvent& ev) {
    if (m_client_fd < 0) return;
    std::string msg = ev.payload_json + "\n";
    send(m_client_fd, msg.c_str(), msg.size(), 0);
}

}


