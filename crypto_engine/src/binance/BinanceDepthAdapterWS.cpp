#include "binance/BinanceDepthAdapterWS.hpp"
#include "binance/BinanceDepthParser.hpp"
#include "binance/BinanceStreams.hpp"
#include "binance/BinanceSubscribe.hpp"
#include "binance/TlsWebSocket.hpp"

#include <thread>

namespace binance {

void BinanceDepthAdapterWS::start(const DepthCallback& cb) {
    running.store(true);

    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT"};
    std::string url = build_ws_url(symbols);

    // parse host/path
    std::string host = "stream.binance.com";
    std::string path = "/stream" + build_stream_query(symbols);

    th = std::thread([this, cb, host, path, symbols]() {
        TlsWebSocket ws(host, 9443, path);
        if (!ws.connect())
            return;

        ws.set_on_message([cb](const std::string& msg) {
            auto parsed = BinanceDepthParser::parse(msg);
            if (parsed)
                cb(*parsed);
        });

        ws.run();
    });
}

void BinanceDepthAdapterWS::stop() {
    running.store(false);
    if (th.joinable())
        th.join();
}

}
