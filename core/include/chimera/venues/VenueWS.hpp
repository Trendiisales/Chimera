#pragma once
#include <string>

namespace chimera::venues {

class VenueWS {
public:
    virtual ~VenueWS() = default;
    virtual void connect(const std::string& url) = 0;
    virtual void close() = 0;
};

class BinanceWS : public VenueWS {
public:
    void connect(const std::string& url) override;
    void close() override;
};

class BybitWS : public VenueWS {
public:
    void connect(const std::string& url) override;
    void close() override;
};

class OKXWS : public VenueWS {
public:
    void connect(const std::string& url) override;
    void close() override;
};

}
