#pragma once

#include "DeskSnapshot.hpp"
#include <mutex>

namespace chimera {
namespace telemetry {

// FIX #7: Double-buffer pattern instead of atomic struct
class SnapshotPublisher {
public:
    SnapshotPublisher() = default;

    void update(const DeskSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer = snapshot;
    }

    DeskSnapshot read() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buffer;
    }

private:
    mutable std::mutex m_mutex;
    DeskSnapshot m_buffer;
};

} // namespace telemetry
} // namespace chimera
