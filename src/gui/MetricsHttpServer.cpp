#include "gui/MetricsHttpServer.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>

static std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

MetricsHttpServer::MetricsHttpServer(int port)
    : port_(port) {}

MetricsHttpServer::~MetricsHttpServer() {
    stop();
}

void MetricsHttpServer::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&MetricsHttpServer::run, this);
}

void MetricsHttpServer::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
}

void MetricsHttpServer::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) return;
    if (listen(server_fd, 8) < 0) return;

    std::cout << "[GUI] http://localhost:" << port_ << "\n";

    while (running_.load()) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;

        char buf[1024]{};
        recv(client, buf, sizeof(buf) - 1, 0);

        std::string req(buf);
        std::string body;
        std::string content_type = "text/plain";

        if (req.find("GET /metrics") == 0) {
            body = read_file("metrics_out/metrics.txt");
        } else {
            body = read_file("dashboard/index.html");
            content_type = "text/html";
        }

        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n\r\n"
             << body;

        std::string out = resp.str();
        send(client, out.c_str(), out.size(), 0);
        close(client);
    }

    close(server_fd);
}
