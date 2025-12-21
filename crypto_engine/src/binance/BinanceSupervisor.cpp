#include "binance/BinanceSupervisor.hpp"

#include <iostream>
#include <memory>
#include <unordered_map>

namespace binance {

struct BookCtx {
    std::unique_ptr<BinaryLogWriter> blog;
};

static std::unordered_map<std::string, BookCtx> g_books;

BinanceSupervisor::BinanceSupervisor(
    BinanceRestClient& rest,
    const std::string& log_dir,
    int metrics_port,
    const std::string& venue
)
    : rest_(rest),
      log_dir_(log_dir),
      metrics_port_(metrics_port),
      venue_(venue)
{
    for (const auto& symbol : {"BTCUSDT", "ETHUSDT"}) {
        BookCtx b;
        b.blog = std::make_unique<BinaryLogWriter>(
            log_dir_ + "/" + symbol + ".blog"
        );
        g_books.emplace(symbol, std::move(b));
        std::cout << "[SUPERVISOR] Added symbol " << symbol << "\n";
    }
}

void BinanceSupervisor::set_pnl_callback(PnlCallback cb) {
    for (auto& kv : g_books) {
        kv.second.blog->set_pnl_callback(cb);
    }
}

} // namespace binance
