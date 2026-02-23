#include "platform/platform.hpp"
#include "TelemetryServer.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif
#include <cstring>
#include <fstream>
#include <sstream>

namespace chimera {

static std::string load(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TelemetryServer::TelemetryServer() : running_(false), server_fd_(-1) {}
TelemetryServer::~TelemetryServer() { stop(); }

void TelemetryServer::start(int port) {
    running_ = true;
    thread_ = std::thread(&TelemetryServer::run, this, port);
}

void TelemetryServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        plat::socket_shutdown(client);
        plat::socket_close(server_fd_);
        server_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

void TelemetryServer::run(int port) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) return;
    if (listen(server_fd_, 16) < 0) return;

    while (running_) {
        int c = accept(server_fd_, nullptr, nullptr);
        if (c < 0) break;

        char buf[512];
        plat::socket_recv(c, buf, sizeof(buf));

        std::string path = "/index.html";
        if (strstr(buf, "GET /style.css")) path = "/style.css";
        if (strstr(buf, "GET /app.js")) path = "/app.js";
        if (strstr(buf, "GET /logo.svg")) path = "/logo.svg";

        std::string body = load("../src/gui/www" + path);
        std::string hdr =
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n\r\n";

        plat::socket_send(c, hdr.data(), hdr.size());
        plat::socket_send(c, body.data(), body.size());
        plat::socket_close(c);
    }
}

}


