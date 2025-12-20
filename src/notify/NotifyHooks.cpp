#include "notify/NotifyHooks.hpp"
#include "notify/NotificationSink.hpp"

using namespace Chimera;

void notify_critical(uint16_t code, uint8_t level) {
    notifier().emit(code, level);
}
