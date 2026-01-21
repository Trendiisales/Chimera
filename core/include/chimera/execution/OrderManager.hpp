#pragma once

#include <string>
#include <unordered_map>
#include "chimera/execution/ExchangeIO.hpp"

namespace chimera {

enum class OrderState {
    IDLE,
    SUBMITTED,
    ACKED,
    PARTIAL,
    FILLED,
    CANCELLED,
    REJECTED
};

struct ManagedOrder {
    std::string client_id;
    std::string symbol;
    bool is_buy = false;
    double qty = 0.0;
    double price = 0.0;

    OrderState state = OrderState::IDLE;
    double filled_qty = 0.0;
    double avg_fill_price = 0.0;

    uint64_t submit_ts = 0;
};

class OrderManager {
public:
    explicit OrderManager(IExchangeIO& io);

    void submit(const OrderRequest& req);
    void cancel(const std::string& client_id);
    void onExchangeUpdate(const OrderUpdate& up);
    void poll();
    void killAll();

    const std::unordered_map<std::string, ManagedOrder>& orders() const;

private:
    IExchangeIO& exchange;
    std::unordered_map<std::string, ManagedOrder> live_orders;
};

}
