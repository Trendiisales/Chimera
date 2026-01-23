#include "chimera/causal/replay.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace chimera::causal {

ReplayEngine::ReplayEngine(const std::string& path)
    : file_path(path), file_size(0) {
    validate_file();
}

void ReplayEngine::validate_file() {
    if (!std::filesystem::exists(file_path)) {
        throw std::runtime_error("Replay file not found: " + file_path);
    }
    file_size = std::filesystem::file_size(file_path);
}

ReplayStream ReplayEngine::load() {
    ReplayStream stream;
    std::ifstream in(file_path, std::ios::binary);
    
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open replay file: " + file_path);
    }
    
    // Read all event headers
    // Each event is variable size but starts with EventHeader
    while (in.good() && !in.eof()) {
        EventHeader h;
        in.read(reinterpret_cast<char*>(&h), sizeof(EventHeader));
        
        if (in.gcount() != sizeof(EventHeader)) {
            break; // End of file or partial read
        }
        
        stream.headers.push_back(h);
        
        // Calculate remaining bytes based on event type
        size_t remaining_bytes = 0;
        switch (h.type) {
            case EventType::TICK:
                remaining_bytes = sizeof(TickEvent) - sizeof(EventHeader);
                break;
            case EventType::DECISION:
                remaining_bytes = sizeof(DecisionEvent) - sizeof(EventHeader);
                break;
            case EventType::RISK:
                remaining_bytes = sizeof(RiskEvent) - sizeof(EventHeader);
                break;
            case EventType::ORDER_INTENT:
                remaining_bytes = sizeof(OrderIntentEvent) - sizeof(EventHeader);
                break;
            case EventType::VENUE_ACK:
                remaining_bytes = sizeof(VenueAckEvent) - sizeof(EventHeader);
                break;
            case EventType::FILL:
                remaining_bytes = sizeof(FillEvent) - sizeof(EventHeader);
                break;
            case EventType::PNL_ATTRIBUTION:
                remaining_bytes = sizeof(PnLAttributionEvent) - sizeof(EventHeader);
                break;
            default:
                remaining_bytes = 0;
        }
        
        // Skip remaining bytes (we only need headers for most analysis)
        if (remaining_bytes > 0) {
            in.seekg(remaining_bytes, std::ios::cur);
        }
    }
    
    return stream;
}

ReplayStream ReplayEngine::load_range(uint64_t start_ts_ns, uint64_t end_ts_ns) {
    ReplayStream full = load();
    ReplayStream filtered;
    
    for (size_t i = 0; i < full.headers.size(); ++i) {
        const auto& h = full.headers[i];
        if (h.ts_ns >= start_ts_ns && h.ts_ns <= end_ts_ns) {
            filtered.headers.push_back(h);
        }
    }
    
    return filtered;
}

size_t ReplayEngine::count_events(EventType type) const {
    std::ifstream in(file_path, std::ios::binary);
    size_t count = 0;
    
    while (in.good() && !in.eof()) {
        EventHeader h;
        in.read(reinterpret_cast<char*>(&h), sizeof(EventHeader));
        
        if (in.gcount() != sizeof(EventHeader)) {
            break;
        }
        
        if (h.type == type) {
            ++count;
        }
        
        // Skip remaining bytes
        size_t remaining_bytes = 0;
        switch (h.type) {
            case EventType::TICK:
                remaining_bytes = sizeof(TickEvent) - sizeof(EventHeader);
                break;
            case EventType::DECISION:
                remaining_bytes = sizeof(DecisionEvent) - sizeof(EventHeader);
                break;
            case EventType::RISK:
                remaining_bytes = sizeof(RiskEvent) - sizeof(EventHeader);
                break;
            case EventType::ORDER_INTENT:
                remaining_bytes = sizeof(OrderIntentEvent) - sizeof(EventHeader);
                break;
            case EventType::VENUE_ACK:
                remaining_bytes = sizeof(VenueAckEvent) - sizeof(EventHeader);
                break;
            case EventType::FILL:
                remaining_bytes = sizeof(FillEvent) - sizeof(EventHeader);
                break;
            case EventType::PNL_ATTRIBUTION:
                remaining_bytes = sizeof(PnLAttributionEvent) - sizeof(EventHeader);
                break;
            default:
                remaining_bytes = 0;
        }
        
        if (remaining_bytes > 0) {
            in.seekg(remaining_bytes, std::ios::cur);
        }
    }
    
    return count;
}

}
