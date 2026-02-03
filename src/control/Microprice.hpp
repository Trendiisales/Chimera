#pragma once
#include <cstdint>

namespace chimera {

class Microprice {
public:
    Microprice();

    void update(double bid, double ask, double bid_qty, double ask_qty);
    double microprice() const;
    double drift() const;

private:
    double m_bid;
    double m_ask;
    double m_bid_qty;
    double m_ask_qty;
    double m_last_micro;
    double m_drift;
};

}
