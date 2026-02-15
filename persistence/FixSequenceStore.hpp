#pragma once
#include <fstream>
#include <string>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>

struct FixSeqState
{
    uint64_t quote_seq = 0;
    uint64_t trade_seq = 0;
    uint64_t dropcopy_seq = 0;
};

class FixSequenceStore
{
public:
    void save(const FixSeqState& s)
    {
        // Write to temp file first (atomic)
        std::string temp_file = "fix_seq.dat.tmp";
        
        // Use POSIX write with fsync for durability
        int fd = ::open(temp_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            throw std::runtime_error("Failed to open FIX sequence file");

        ssize_t written = ::write(fd, &s, sizeof(s));
        if (written != sizeof(s))
        {
            ::close(fd);
            throw std::runtime_error("Failed to write FIX sequence");
        }

        // CRITICAL: fsync for crash durability
        ::fsync(fd);
        ::close(fd);

        // Atomic rename
        std::rename(temp_file.c_str(), "fix_seq.dat");

        std::cout << "[FIX SEQ] Saved: QUOTE=" << s.quote_seq 
                  << " TRADE=" << s.trade_seq 
                  << " DROPCOPY=" << s.dropcopy_seq << std::endl;
    }

    FixSeqState load()
    {
        FixSeqState s{};
        std::ifstream in("fix_seq.dat", std::ios::binary);
        if(in.good())
        {
            in.read((char*)&s, sizeof(s));
            std::cout << "[FIX SEQ] Loaded: QUOTE=" << s.quote_seq 
                      << " TRADE=" << s.trade_seq 
                      << " DROPCOPY=" << s.dropcopy_seq << std::endl;
        }
        else
        {
            std::cout << "[FIX SEQ] No previous state, starting fresh" << std::endl;
        }
        return s;
    }
};
