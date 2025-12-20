#pragma once
#include "notify/NotificationTypes.hpp"
#include <string>

namespace Chimera {

class NotificationSink {
public:
    explicit NotificationSink(const std::string& path);

    void emit(uint16_t code, uint8_t level);

private:
    std::string path_;
};

NotificationSink& notifier();

}
