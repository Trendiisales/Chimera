#include "gui/gui_server.hpp"
#include <sstream>
#include <thread>
#include <netinet/in.h>
#include <unistd.h>

GUIServer::GUIServer(int p, ChimeraTelemetry& t)
    : port_(p), telemetry_(t) {}

void GUIServer::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);

        std::ostringstream out;
        out << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        out << "{";
        out << "\"online\":" << (telemetry_.online ? "true" : "false") << ",";
        out << "\"trading\":" << (telemetry_.trading ? "true" : "false") << ",";
        out << "\"btc_price\":" << telemetry_.btc_price.load() << ",";
        out << "\"eth_price\":" << telemetry_.eth_price.load() << ",";
        out << "\"trades\":" << telemetry_.trades.load() << ",";
        out << "\"pnl_bps\":" << telemetry_.realized_pnl_bps.load();
        out << "}";

        std::string s = out.str();
        send(client, s.c_str(), s.size(), 0);
        close(client);
    }
}
