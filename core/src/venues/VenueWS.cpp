#include "chimera/venues/VenueWS.hpp"
#include <iostream>

namespace chimera::venues {

void BinanceWS::connect(const std::string& url) {
    std::cout << "[BinanceWS] Connecting to: " << url << std::endl;
    // WebSocket connection logic
}

void BinanceWS::close() {
    std::cout << "[BinanceWS] Closing connection" << std::endl;
}

void BybitWS::connect(const std::string& url) {
    std::cout << "[BybitWS] Connecting to: " << url << std::endl;
}

void BybitWS::close() {
    std::cout << "[BybitWS] Closing connection" << std::endl;
}

void OKXWS::connect(const std::string& url) {
    std::cout << "[OKXWS] Connecting to: " << url << std::endl;
}

void OKXWS::close() {
    std::cout << "[OKXWS] Closing connection" << std::endl;
}

}
