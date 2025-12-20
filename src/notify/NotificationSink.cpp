#include "notify/NotificationSink.hpp"
#include <chrono>
#include <fstream>

using namespace Chimera;

static NotificationSink g_sink("notify_out/alerts.log");

NotificationSink::NotificationSink(const std::string& path)
    : path_(path) {}

NotificationSink& Chimera::notifier() {
    return g_sink;
}

void NotificationSink::emit(uint16_t code, uint8_t level) {
    uint64_t ts_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

    std::ofstream f(path_, std::ios::app);
    f << ts_ns << ","
      << code << ","
      << static_cast<uint32_t>(level) << ","
      << static_cast<uint32_t>(NotifyChannel::LOCAL_LOG)
      << "\n";
}
