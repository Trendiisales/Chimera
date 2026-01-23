#pragma once
#include <unordered_map>
#include <string>

namespace chimera::arbitrage {

class CrossVenueArb {
public:
    void update(const std::string& venue, const std::string& sym, double bid, double ask) {
        book[venue + sym] = {bid, ask};
    }

    bool opportunity(const std::string& a, const std::string& b, const std::string& sym, double min_bps) {
        auto ka = a + sym;
        auto kb = b + sym;
        if (!book.count(ka) || !book.count(kb)) return false;

        auto pa = book[ka];
        auto pb = book[kb];

        double spread = (pb.bid - pa.ask) / pa.ask * 10000.0;
        return spread > min_bps;
    }

private:
    struct Quote { double bid, ask; };
    std::unordered_map<std::string, Quote> book;
};

}
