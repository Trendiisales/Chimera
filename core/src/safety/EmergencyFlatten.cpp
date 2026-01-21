#include "chimera/safety/EmergencyFlatten.hpp"
#include <cmath>

namespace chimera {

EmergencyFlatten::EmergencyFlatten(
    PositionBook& book,
    OrderManager& orders
) : position_book(book),
    order_manager(orders) {}

void EmergencyFlatten::flattenAll() {
    // Note: Requires PositionBook::all() method
    // Will be added to PositionBook
    for (const auto& kv :
         position_book.all()) {

        const std::string& sym =
            kv.first;
        const Position& pos =
            kv.second;

        if (pos.net_qty == 0.0) {
            continue;
        }

        OrderRequest req;
        req.client_id =
            "FLATTEN_" + sym;
        req.symbol = sym;
        req.qty =
            std::abs(pos.net_qty);
        req.is_buy =
            pos.net_qty < 0.0;
        req.market = true;

        order_manager.submit(req);
    }
}

}
