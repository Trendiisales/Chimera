#include "chimera/execution/OrderManager.hpp"
#include <chrono>

namespace chimera {

static uint64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

OrderManager::OrderManager(IExchangeIO& io)
    : exchange(io) {}

void OrderManager::submit(const OrderRequest& req) {
    ManagedOrder ord;
    ord.client_id = req.client_id;
    ord.symbol = req.symbol;
    ord.is_buy = req.is_buy;
    ord.qty = req.qty;
    ord.price = req.price;
    ord.state = OrderState::SUBMITTED;
    ord.submit_ts = nowNs();

    live_orders[req.client_id] = ord;
    exchange.sendOrder(req);
}

void OrderManager::cancel(const std::string& client_id) {
    exchange.cancelOrder(client_id);
}

void OrderManager::onExchangeUpdate(const OrderUpdate& up) {
    auto it = live_orders.find(up.client_id);
    if (it == live_orders.end()) return;

    ManagedOrder& ord = it->second;

    ord.filled_qty = up.filled_qty;
    ord.avg_fill_price = up.avg_price;

    if (up.status == "ACK") {
        ord.state = OrderState::ACKED;
    } else if (up.status == "PARTIAL") {
        ord.state = OrderState::PARTIAL;
    } else if (up.status == "FILLED") {
        ord.state = OrderState::FILLED;
    } else if (up.status == "CANCELLED") {
        ord.state = OrderState::CANCELLED;
    } else if (up.status == "REJECTED") {
        ord.state = OrderState::REJECTED;
    }

    if (up.is_final) {
        live_orders.erase(it);
    }
}

void OrderManager::poll() {
    exchange.poll();
}

void OrderManager::killAll() {
    for (auto& kv : live_orders) {
        exchange.cancelOrder(kv.first);
    }
}

const std::unordered_map<std::string, ManagedOrder>&
OrderManager::orders() const {
    return live_orders;
}

}
