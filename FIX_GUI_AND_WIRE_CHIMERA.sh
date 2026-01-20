#!/usr/bin/env bash
set -e

ROOT="$HOME/chimera_work"
MAIN="$ROOT/main.cpp"
GUI="$ROOT/gui/live_operator_server.cpp"

echo "[CHIMERA] Installing operator console"

mkdir -p "$ROOT/gui"

echo "[CHIMERA] Writing GUI server"
cat > "$GUI" << 'GUICODE'
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <sstream>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>

static std::mutex g_gui_lock;
static std::string g_gui_html;

void gui_set_html(const std::string& html) {
    std::lock_guard<std::mutex> lock(g_gui_lock);
    g_gui_html = html;
}

static std::string gui_get_html() {
    std::lock_guard<std::mutex> lock(g_gui_lock);
    return g_gui_html;
}

static std::string build_page() {
    std::ostringstream o;
    o <<
    "<html><head>"
    "<meta http-equiv='refresh' content='1'>"
    "<title>CHIMERA OPERATOR</title>"
    "<style>"
    "body{background:#111;color:#ddd;font-family:monospace;padding:20px}"
    "h1{color:#00bfff}"
    "pre{background:#000;padding:10px;border:1px solid #333}"
    "</style>"
    "</head><body>"
    "<h1>CHIMERA OPERATOR CONSOLE</h1>"
    "<pre>" << gui_get_html() << "</pre>"
    "</body></html>";
    return o.str();
}

static void serve_client(int client) {
    std::string body = build_page();

    std::ostringstream resp;
    resp <<
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: " << body.size() << "\r\n"
        "Connection: close\r\n\r\n"
        << body;

    std::string out = resp.str();
    send(client, out.c_str(), out.size(), 0);
    close(client);
}

void start_operator_console(int port) {
    std::thread([port]() {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            perror("socket");
            return;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            return;
        }

        if (listen(server_fd, 16) < 0) {
            perror("listen");
            return;
        }

        std::cout << "[GUI] Operator console listening on " << port << std::endl;

        while (true) {
            int client = accept(server_fd, nullptr, nullptr);
            if (client >= 0) {
                serve_client(client);
            }
        }
    }).detach();
}
GUICODE

echo "[CHIMERA] Injecting include"
grep -q 'live_operator_server.cpp' "$MAIN" || \
sed -i '1i #include "gui/live_operator_server.cpp"' "$MAIN"

echo "[CHIMERA] Injecting startup call"
grep -q 'start_operator_console' "$MAIN" || \
sed -i '/int main/ a \ \ \ \ start_operator_console(8080);' "$MAIN"

echo "[CHIMERA] Wiring FLOW output to GUI"
sed -i 's/\\[FLOW\\]/[FLOW]/g' "$MAIN"
sed -i '/\\[FLOW\\]/a \ \ \ \ gui_set_html(flow_string);' "$MAIN" || true

echo "[CHIMERA] Rebuilding"
cd "$ROOT/build"
make -j

echo
echo "[CHIMERA] DONE"
echo "Open in browser:"
echo "http://15.168.16.103:8080"
