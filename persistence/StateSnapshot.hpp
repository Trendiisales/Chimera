#pragma once
#include <fstream>
#include <string>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>

struct SymbolSnapshot
{
    uint32_t version;        // Snapshot format version (current: 1)
    double pos_size;
    double pos_avg;
    double realized;
    double capital_loss;
    uint64_t last_event_ts;
};

class StateSnapshot
{
public:
    void save(const std::string& symbol, const SymbolSnapshot& s)
    {
        // Ensure version is set
        SymbolSnapshot versioned = s;
        versioned.version = 1;

        // Write to temp file first (atomic)
        std::string temp_file = symbol + "_snapshot.bin.tmp";
        
        // Use POSIX write with fsync for durability
        int fd = ::open(temp_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            throw std::runtime_error("Failed to open snapshot file");

        ssize_t written = ::write(fd, &versioned, sizeof(versioned));
        if (written != sizeof(versioned))
        {
            ::close(fd);
            throw std::runtime_error("Failed to write snapshot");
        }

        // CRITICAL: fsync for crash durability
        ::fsync(fd);
        ::close(fd);

        // Atomic rename
        std::string final_file = symbol + "_snapshot.bin";
        std::rename(temp_file.c_str(), final_file.c_str());

        std::cout << "[SNAPSHOT:" << symbol << "] Saved v" << versioned.version 
                  << ": pos=" << versioned.pos_size 
                  << " avg=" << versioned.pos_avg 
                  << " realized=" << versioned.realized << std::endl;
    }

    SymbolSnapshot load(const std::string& symbol)
    {
        SymbolSnapshot s{};
        std::string filename = symbol + "_snapshot.bin";
        std::ifstream in(filename, std::ios::binary);
        
        if(in.good())
        {
            in.read((char*)&s, sizeof(s));
            
            // Validate version
            const uint32_t CURRENT_VERSION = 1;
            if (s.version != CURRENT_VERSION)
            {
                std::cerr << "[SNAPSHOT:" << symbol << "] Version mismatch: file=" 
                          << s.version << " expected=" << CURRENT_VERSION 
                          << " - ignoring snapshot" << std::endl;
                return SymbolSnapshot{CURRENT_VERSION, 0, 0, 0, 0, 0};
            }
            
            std::cout << "[SNAPSHOT:" << symbol << "] Loaded v" << s.version 
                      << ": pos=" << s.pos_size 
                      << " avg=" << s.pos_avg 
                      << " realized=" << s.realized << std::endl;
        }
        else
        {
            std::cout << "[SNAPSHOT:" << symbol << "] No previous snapshot" << std::endl;
            s.version = 1;
        }
        
        return s;
    }
};
