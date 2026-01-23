#pragma once
#include "chimera/venue/VenueExecutionIO.hpp"
#include <string>

namespace chimera::venue {

class OKXIO : public VenueExecutionIO {
public:
    OKXIO(const std::string& key, const std::string& secret, const std::string& pass);

    void connect() override;
    void send(const VenueOrder& o) override;

private:
    std::string api_key;
    std::string api_secret;
    std::string api_pass;
};

}
