#include "../core/contract.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <thread>
#include <string.h>

using namespace chimera;

static std::string json(const ChimeraTelemetry& t) {
    std::ostringstream o;
    o << "{"
      << "\"online\":" << (t.online ? "true" : "false") << ","
      << "\"trading\":" << (t.trading ? "true" : "false") << ","
      << "\"btc_price\":" << t.btc_price << ","
      << "\"eth_price\":" << t.eth_price << ","
      << "\"trades\":" << t.trades
      << "}";
    return o.str();
}

void run_gui(const ChimeraTelemetry* t) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;

        char buf[1024];
        read(client, buf, sizeof(buf));

        std::string body = json(*t);
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n\r\n"
             << body;

        std::string out = resp.str();
        send(client, out.c_str(), out.size(), 0);
        close(client);
    }
}
