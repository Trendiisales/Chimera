#include "chimera/venue/BybitIO.hpp"
#include <iostream>

using namespace chimera::venue;

BybitIO::BybitIO(const std::string& k, const std::string& s)
: api_key(k), api_secret(s) {}

void BybitIO::connect() {
    std::cout << "[BYBIT] Connecting WS + REST" << std::endl;
}

void BybitIO::send(const VenueOrder& o) {
    std::cout << "[BYBIT] ORDER " << o.symbol << " " << o.side << " @" << o.price << std::endl;

    if (ack_cb) {
        ack_cb({"BYBIT", "SIM_ACK", true});
    }

    if (fill_cb) {
        fill_cb({"BYBIT", o.symbol, o.qty, o.price});
    }
}
