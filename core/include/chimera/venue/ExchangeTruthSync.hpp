#pragma once
#include <string>
#include <vector>

namespace chimera::venue {

struct OpenOrder {
    std::string symbol;
    std::string order_id;
    double qty;
};

struct Position {
    std::string symbol;
    double qty;
};

class ExchangeTruthSync {
public:
    virtual ~ExchangeTruthSync() = default;

    virtual std::vector<OpenOrder> fetchOpenOrders() = 0;
    virtual std::vector<Position> fetchPositions() = 0;

    template<typename LocalBook>
    void reconcile(LocalBook& local) {
        auto orders = fetchOpenOrders();
        auto pos = fetchPositions();

        local.clearUnknownOrders(orders);
        local.syncPositions(pos);
    }
};

}
