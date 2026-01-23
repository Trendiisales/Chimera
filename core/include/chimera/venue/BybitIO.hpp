#pragma once
#include "chimera/venue/VenueExecutionIO.hpp"
#include <string>

namespace chimera::venue {

class BybitIO : public VenueExecutionIO {
public:
    BybitIO(const std::string& key, const std::string& secret);

    void connect() override;
    void send(const VenueOrder& o) override;

private:
    std::string api_key;
    std::string api_secret;
};

}
