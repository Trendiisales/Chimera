#pragma once
#include <string>
#include <functional>

namespace chimera::venue {

struct VenueOrder {
    std::string symbol;
    std::string side;
    double qty;
    double price;
};

struct VenueAck {
    std::string venue;
    std::string order_id;
    bool accepted;
};

struct VenueFill {
    std::string venue;
    std::string symbol;
    double qty;
    double price;
};

class VenueExecutionIO {
public:
    using AckHandler = std::function<void(const VenueAck&)>;
    using FillHandler = std::function<void(const VenueFill&)>;

    virtual ~VenueExecutionIO() = default;

    virtual void connect() = 0;
    virtual void send(const VenueOrder& o) = 0;

    void onAck(AckHandler h) { ack_cb = h; }
    void onFill(FillHandler h) { fill_cb = h; }

protected:
    AckHandler ack_cb;
    FillHandler fill_cb;
};

}
