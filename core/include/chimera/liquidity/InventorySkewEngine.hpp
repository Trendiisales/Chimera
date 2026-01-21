#pragma once

#include <string>

#include "chimera/execution/PositionBook.hpp"

namespace chimera {

class InventorySkewEngine {
public:
    explicit InventorySkewEngine(PositionBook& book);

    double skewBps(
        const std::string& symbol
    ) const;

    void setMaxSkewBps(double bps);
    void setSkewPerUnit(double bps);

private:
    PositionBook& position_book;

    double max_skew_bps = 5.0;
    double skew_per_unit = 1.0;
};

}
