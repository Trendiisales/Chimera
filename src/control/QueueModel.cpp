#include "control/QueueModel.hpp"

namespace chimera {

QueueModel::QueueModel()
: m_queue_depth(1.0),
  m_flow_rate(0.1) {}

void QueueModel::on_book(double level_qty, double traded_qty) {
    m_queue_depth = level_qty > 0.0 ? level_qty : 1.0;
    double k = 0.2;
    m_flow_rate = (1.0 - k) * m_flow_rate + k * traded_qty;
}

double QueueModel::fill_probability(double my_qty) const {
    if (m_queue_depth <= 0.0)
        return 1.0;

    double position = my_qty / m_queue_depth;
    double prob = m_flow_rate / (m_flow_rate + position + 1e-6);

    if (prob < 0.0) prob = 0.0;
    if (prob > 1.0) prob = 1.0;
    return prob;
}

}
