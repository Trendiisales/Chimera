#include "control/Microprice.hpp"

namespace chimera {

Microprice::Microprice()
: m_bid(0.0),
  m_ask(0.0),
  m_bid_qty(1.0),
  m_ask_qty(1.0),
  m_last_micro(0.0),
  m_drift(0.0) {}

void Microprice::update(double bid, double ask, double bid_qty, double ask_qty) {
    m_bid = bid;
    m_ask = ask;
    m_bid_qty = bid_qty > 0.0 ? bid_qty : 1.0;
    m_ask_qty = ask_qty > 0.0 ? ask_qty : 1.0;

    // Volume-weighted price
    double micro = (m_bid * m_ask_qty + m_ask * m_bid_qty) /
                   (m_bid_qty + m_ask_qty);

    m_drift = micro - m_last_micro;
    m_last_micro = micro;
}

double Microprice::microprice() const {
    return m_last_micro;
}

double Microprice::drift() const {
    return m_drift;
}

}
