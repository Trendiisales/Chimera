#include "telemetry/HttpServer.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace chimera;
namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp = asio::ip::tcp;

HttpServer::HttpServer(uint16_t port, Context& ctx)
    : port_(port), ctx_(ctx) {}

void HttpServer::run() {
    try {
        asio::io_context ioc;
        tcp::acceptor acceptor(ioc, {tcp::v4(), port_});
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.non_blocking(true);

        std::cout << "[TELEMETRY] Listening on " << port_ << "\n";

        while (ctx_.running.load()) {
            tcp::socket socket(ioc);
            beast::error_code ec;
            acceptor.accept(socket, ec);

            if (ec == asio::error::would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (ec) continue;

            try {
                beast::flat_buffer buffer;
                http::request<http::string_body> req;
                http::read(socket, buffer, req);

                http::response<http::string_body> res;
                res.version(req.version());
                res.result(http::status::ok);
                res.set(http::field::server, "chimera");

                if (req.target() == "/metrics") {
                    res.set(http::field::content_type, "text/plain");
                    res.body() = ctx_.telemetry.to_prometheus();
                } else {
                    res.set(http::field::content_type, "application/json");
                    res.body() = ctx_.telemetry.to_json();
                }

                res.prepare_payload();
                http::write(socket, res);
            } catch (...) {
                // Malformed request — drop, continue
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[TELEMETRY] " << e.what() << "\n";
    }
}
