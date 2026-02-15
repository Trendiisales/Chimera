#pragma once
#include <fstream>
#include <vector>
#include <cstdint>
#include <iostream>
#include <functional>
#include "BinaryJournal.hpp"

class ReplayEngine
{
public:
    using EventCallback = std::function<void(uint8_t type, const std::vector<uint8_t>& payload, uint64_t timestamp)>;

    void replay(const std::string& file, EventCallback callback)
    {
        std::ifstream in(file, std::ios::binary);
        if(!in.good())
        {
            std::cout << "[REPLAY] File not found: " << file << std::endl;
            return;
        }

        std::cout << "[REPLAY] Starting replay: " << file << std::endl;

        int event_count = 0;
        int corrupted_count = 0;

        while(true)
        {
            EventHeader hdr;
            if(!in.read((char*)&hdr, sizeof(hdr)))
                break;

            // Validate header sanity
            if(hdr.data_len > 1024 * 1024)  // 1MB max event size
            {
                std::cerr << "[REPLAY] Abnormal event size: " << hdr.data_len << " bytes, stopping" << std::endl;
                break;
            }

            std::vector<uint8_t> payload(hdr.data_len);
            if(!in.read((char*)payload.data(), hdr.data_len))
            {
                std::cerr << "[REPLAY] Truncated event, stopping" << std::endl;
                break;
            }

            // CRITICAL: Validate CRC
            uint32_t computed_crc = crc32_compute(payload.data(), payload.size());
            if(computed_crc != hdr.crc32)
            {
                std::cerr << "[REPLAY] CRC mismatch at event " << event_count 
                          << " (expected: " << hdr.crc32 
                          << ", got: " << computed_crc << ")" << std::endl;
                corrupted_count++;
                continue;  // Skip corrupted event
            }

            // Valid event - process it
            callback(hdr.event_type, payload, hdr.timestamp_ns);
            event_count++;
        }

        std::cout << "[REPLAY] Complete: " << event_count << " events replayed";
        if(corrupted_count > 0)
            std::cout << ", " << corrupted_count << " corrupted events skipped";
        std::cout << std::endl;
    }

    void replay_all_segments(const std::string& base_pattern, EventCallback callback)
    {
        // Find all matching journal files
        std::vector<std::string> files;
        // Implementation: scan directory for base_pattern*.bin files
        // Sort by timestamp
        // Replay in order
        
        std::cout << "[REPLAY] Multi-segment replay for pattern: " << base_pattern << std::endl;
        
        // For now, replay single file
        replay(base_pattern + ".bin", callback);
    }
};
