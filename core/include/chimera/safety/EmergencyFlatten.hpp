#pragma once

#include "chimera/execution/PositionBook.hpp"
#include "chimera/execution/OrderManager.hpp"

namespace chimera {

class EmergencyFlatten {
public:
    EmergencyFlatten(
        PositionBook& book,
        OrderManager& orders
    );

    void flattenAll();

private:
    PositionBook& position_book;
    OrderManager& order_manager;
};

}
