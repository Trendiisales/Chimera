#pragma once
#include <cstdint>

namespace chimera {

class QueueModel {
public:
    QueueModel();

    void on_book(double level_qty, double traded_qty);
    double fill_probability(double my_qty) const;

private:
    double m_queue_depth;
    double m_flow_rate;
};

}
