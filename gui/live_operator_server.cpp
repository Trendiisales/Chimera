#include <thread>
#include <iostream>
#include <sstream>

#include "httplib.h"
#include "gui_feed.hpp"

static std::thread gui_thread;

void start_operator_console(int port) {
    gui_thread = std::thread([port]() {
        httplib::Server svr;

        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            std::ostringstream body;
            body << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                 << "<title>CHIMERA LIVE</title>"
                 << "<style>"
                 << "body{background:#111;color:#ddd;font-family:monospace;margin:20px}"
                 << "h1{color:#aaa}"
                 << "pre{background:#000;padding:15px;border-radius:5px;white-space:pre-wrap}"
                 << "</style></head><body>"
                 << "<h1>CHIMERA LIVE</h1>"
                 << "<pre>"
                 << gui_get_html()
                 << "</pre>"
                 << "</body></html>";

            res.set_content(body.str(), "text/html");
        });

        std::cout << "[GUI] Operator console listening on 0.0.0.0:" << port << std::endl;
        svr.listen("0.0.0.0", port);
    });

    gui_thread.detach();
}
