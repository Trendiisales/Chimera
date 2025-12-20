#include "binance/PrometheusServer.hpp"
#include "binance/PrometheusMetrics.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

namespace binance {

PrometheusServer::PrometheusServer(int port_)
    : port(port_) {}

PrometheusServer::~PrometheusServer() {
    stop();
}

void PrometheusServer::start() {
    if (running)
        return;
    running = true;
    server_thread = std::thread(&PrometheusServer::run, this);
}

void PrometheusServer::stop() {
    running = false;
    if (server_thread.joinable())
        server_thread.join();
}

void PrometheusServer::run() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        return;

    listen(fd, 16);

    while (running) {
        int cfd = accept(fd, nullptr, nullptr);
        if (cfd < 0)
            continue;

        std::string body =
            PrometheusMetrics::instance().render();

        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4\r\n"
            "Content-Length: " + std::to_string(body.size()) +
            "\r\n\r\n" + body;

        send(cfd, resp.data(), resp.size(), 0);
        close(cfd);
    }

    close(fd);
}

}
