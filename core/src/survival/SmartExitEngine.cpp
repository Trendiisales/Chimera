#include "chimera/survival/SmartExitEngine.hpp"
#include <chrono>
#include <cmath>

namespace chimera {

SmartExitEngine::SmartExitEngine(
    PositionBook& book,
    OrderManager& orders,
    MarketBus& market
) : position_book(book),
    order_manager(orders),
    market_bus(market) {}

void SmartExitEngine::setExitProfile(
    const std::string& symbol,
    const ExitProfile& prof
) {
    profiles[symbol] = prof;
}

void SmartExitEngine::onFill(
    const std::string& symbol,
    bool is_buy,
    double qty,
    double price,
    uint64_t ts_ns
) {
    LiveTrade t;
    t.symbol = symbol;
    t.is_long = is_buy;
    t.qty = qty;
    t.entry_price = price;
    t.open_ts = ts_ns;

    live_trades[symbol] = t;
}

void SmartExitEngine::poll(
    uint64_t now_ns
) {
    for (const auto& kv : live_trades) {
        const std::string& sym = kv.first;
        const LiveTrade& t = kv.second;

        auto pit = profiles.find(sym);
        if (pit == profiles.end()) continue;

        evaluateExit(
            t,
            pit->second,
            now_ns
        );
    }
}

void SmartExitEngine::evaluateExit(
    const LiveTrade& trade,
    const ExitProfile& prof,
    uint64_t now_ns
) {
    double last = market_bus.last(trade.symbol);
    if (last <= 0.0) return;

    double bps =
        (last - trade.entry_price) /
        trade.entry_price * 10000.0;

    if (!trade.is_long) {
        bps = -bps;
    }

    double elapsed =
        (now_ns - trade.open_ts) / 1e9;

    bool take_profit =
        bps >= prof.take_profit_bps;

    bool stop_loss =
        bps <= -prof.stop_loss_bps;

    bool time_exit =
        elapsed >= prof.time_decay_sec;

    if (!take_profit &&
        !stop_loss &&
        !time_exit) {
        return;
    }

    OrderRequest req;
    req.client_id =
        "EXIT_" + trade.symbol;
    req.symbol = trade.symbol;
    req.qty = trade.qty;
    req.price = last;
    req.is_buy = !trade.is_long;

    order_manager.submit(req);
}

}
