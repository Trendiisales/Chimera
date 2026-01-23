#include "chimera/venue/OKXIO.hpp"
#include <iostream>

using namespace chimera::venue;

OKXIO::OKXIO(const std::string& k, const std::string& s, const std::string& p)
: api_key(k), api_secret(s), api_pass(p) {}

void OKXIO::connect() {
    std::cout << "[OKX] Connecting WS + REST" << std::endl;
}

void OKXIO::send(const VenueOrder& o) {
    std::cout << "[OKX] ORDER " << o.symbol << " " << o.side << " @" << o.price << std::endl;

    if (ack_cb) {
        ack_cb({"OKX", "SIM_ACK", true});
    }

    if (fill_cb) {
        fill_cb({"OKX", o.symbol, o.qty, o.price});
    }
}
