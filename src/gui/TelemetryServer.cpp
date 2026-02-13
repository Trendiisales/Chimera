#include "gui/TelemetryServer.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>

TelemetryServer::TelemetryServer(int port, SnapshotFn cb)
    : port_(port), snapshot_callback_(cb) {}

TelemetryServer::~TelemetryServer() {
    stop();
}

void TelemetryServer::start() {
    running_ = true;
    thread_ = std::thread(&TelemetryServer::run, this);
}

void TelemetryServer::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void TelemetryServer::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    std::cout << "[Telemetry] Listening on port " << port_ << std::endl;

    while (running_) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client >= 0)
            handle_client(client);
    }

    close(server_fd);
}

void TelemetryServer::handle_client(int client_fd) {
    char buffer[4096];
    int len = read(client_fd, buffer, sizeof(buffer) - 1);

    if (len <= 0) {
        close(client_fd);
        return;
    }

    buffer[len] = 0;
    std::string request(buffer);

    std::string body;
    std::string content_type = "text/plain";

    if (request.find("GET /snapshot") != std::string::npos) {
        body = snapshot_callback_ ? snapshot_callback_() : "{}";
        content_type = "application/json";
    } else {
        body = "<html><body><h1>Chimera Telemetry Running</h1></body></html>";
        content_type = "text/html";
    }

    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;

    std::string response = oss.str();
    write(client_fd, response.c_str(), response.size());
    close(client_fd);
}
