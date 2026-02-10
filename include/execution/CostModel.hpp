#pragma once
#include <string>

struct CostInput {
    std::string symbol;
    double price;
    double spread_points;
    double size;
    double latency_ms;
};

struct CostOutput {
    double spread_cost;
    double commission_cost;
    double latency_cost;
    double total_cost;
    bool latency_blocked;
};

class CostModel {
public:
    CostModel();
    CostOutput compute(const CostInput& in) const;
    double min_edge_required(const std::string& symbol) const;
private:
    double commission_per_lot_xau_;
    double commission_per_lot_xag_;
    double latency_slip_per_ms_xau_;
    double latency_slip_per_ms_xag_;
    double contract_size_xau_;
    double contract_size_xag_;
    double max_latency_xau_;
    double max_latency_xag_;
    double convex_latency_cost(double latency_ms, bool is_xau) const;
};
