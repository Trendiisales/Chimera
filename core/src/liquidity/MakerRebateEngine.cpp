#include "chimera/liquidity/MakerRebateEngine.hpp"

namespace chimera {

MakerRebateEngine::MakerRebateEngine(
    MarketBus& market,
    EdgeSurvivalFilter& survival,
    OrderManager& orders
) : market_bus(market),
    survival_filter(survival),
    order_manager(orders) {}

void MakerRebateEngine::setConfig(
    const std::string& symbol,
    const QuoteConfig& cfg
) {
    configs[symbol] = cfg;
}

void MakerRebateEngine::cancelAll() {
    for (const auto& kv : live_quotes) {
        order_manager.cancel(kv.second.bid_id);
        order_manager.cancel(kv.second.ask_id);
    }
    live_quotes.clear();
}

void MakerRebateEngine::onTick(
    const std::string& symbol,
    uint64_t
) {
    auto it = configs.find(symbol);
    if (it == configs.end()) return;

    const QuoteConfig& cfg = it->second;

    double spread = market_bus.spread(symbol);
    double last = market_bus.last(symbol);
    double vol = market_bus.volatility(symbol);

    if (last <= 0.0) return;

    double spread_bps =
        (spread / last) * 10000.0;

    if (spread_bps < cfg.min_spread_bps ||
        vol > cfg.max_volatility) {
        cancelQuotes(symbol);
        return;
    }

    double mid = last;

    placeQuotes(
        symbol,
        cfg,
        mid
    );
}

void MakerRebateEngine::cancelQuotes(
    const std::string& symbol
) {
    auto it = live_quotes.find(symbol);
    if (it == live_quotes.end()) return;

    order_manager.cancel(it->second.bid_id);
    order_manager.cancel(it->second.ask_id);

    live_quotes.erase(it);
}

void MakerRebateEngine::placeQuotes(
    const std::string& symbol,
    const QuoteConfig& cfg,
    double mid
) {
    double quote_offset =
        (cfg.quote_bps / 10000.0) * mid;

    double bid_price =
        mid - quote_offset;

    double ask_price =
        mid + quote_offset;

    SurvivalDecision surv =
        survival_filter.evaluate(
            symbol,
            true,
            cfg.min_spread_bps,
            cfg.order_size,
            1.0
        );

    if (!surv.allowed) {
        cancelQuotes(symbol);
        return;
    }

    ActiveQuote& q =
        live_quotes[symbol];

    if (q.bid_id.empty()) {
        OrderRequest bid;
        bid.client_id =
            "MAKER_BID_" + symbol;
        bid.symbol = symbol;
        bid.qty = cfg.order_size;
        bid.price = bid_price;
        bid.is_buy = true;

        order_manager.submit(bid);
        q.bid_id = bid.client_id;
        q.bid_price = bid_price;
    }

    if (q.ask_id.empty()) {
        OrderRequest ask;
        ask.client_id =
            "MAKER_ASK_" + symbol;
        ask.symbol = symbol;
        ask.qty = cfg.order_size;
        ask.price = ask_price;
        ask.is_buy = false;

        order_manager.submit(ask);
        q.ask_id = ask.client_id;
        q.ask_price = ask_price;
    }
}

}
