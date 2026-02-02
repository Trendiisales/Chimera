#pragma once
#include <string>
#include <vector>
#include <atomic>

namespace chimera {

struct VenueOrder {
    std::string client_id;
    std::string symbol;
    double price;
    double qty;
};

struct VenuePosition {
    std::string symbol;
    double qty;
    double entry_price;
};

struct VenueOpenOrder {
    std::string client_id;
    std::string symbol;
    double price;
    double qty;
};

class VenueAdapter {
public:
    virtual ~VenueAdapter() = default;

    virtual std::string name() const = 0;

    // Live streams
    virtual void run_market(std::atomic<bool>& running) = 0;
    virtual void run_user(std::atomic<bool>& running)   = 0;

    // Execution
    virtual bool send_order(const VenueOrder& ord)          = 0;
    virtual bool cancel_order(const std::string& client_id) = 0;

    // Cold-start reconciliation — pull full exchange state
    virtual bool get_all_positions(std::vector<VenuePosition>& out)   = 0;
    virtual bool get_all_open_orders(std::vector<VenueOpenOrder>& out) = 0;
};

}
