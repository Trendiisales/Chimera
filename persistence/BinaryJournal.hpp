#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static uint32_t crc32_compute(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for(size_t i=0;i<len;i++)
    {
        crc ^= data[i];
        for(int j=0;j<8;j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return ~crc;
}

#pragma pack(push,1)
struct EventHeader
{
    uint64_t timestamp_ns;
    uint8_t  event_type;
    uint16_t data_len;
    uint32_t crc32;
};
#pragma pack(pop)

class BinaryJournal
{
public:
    BinaryJournal(const std::string& base)
        : base_name(base)
    {
        open_new_file();
    }

    ~BinaryJournal()
    {
        close();
    }

    void write_event(uint8_t type, const std::vector<uint8_t>& payload)
    {
        EventHeader hdr;
        hdr.timestamp_ns = now_ns();
        hdr.event_type = type;
        hdr.data_len = payload.size();
        hdr.crc32 = crc32_compute(payload.data(), payload.size());

        // Atomic write: header + payload in single buffer
        std::vector<uint8_t> buffer(sizeof(hdr) + payload.size());
        std::memcpy(buffer.data(), &hdr, sizeof(hdr));
        std::memcpy(buffer.data() + sizeof(hdr),
                    payload.data(), payload.size());

        ssize_t written = ::write(fd, buffer.data(), buffer.size());
        if(written != (ssize_t)buffer.size())
            throw std::runtime_error("Journal write failed");

        // CRITICAL: fsync for crash durability
        ::fsync(fd);

        rotate_if_needed();
    }

    void close()
    {
        if(fd >= 0)
        {
            ::fsync(fd);
            ::close(fd);
            fd = -1;
        }
    }

    std::string current_file() const
    {
        return current_filename;
    }

private:
    std::string base_name;
    std::string current_filename;
    int fd = -1;
    size_t max_size = 100 * 1024 * 1024;  // 100MB rotation

    uint64_t now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

    void open_new_file()
    {
        current_filename = base_name + "_" + timestamp_string() + ".bin";
        fd = ::open(current_filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(fd < 0)
            throw std::runtime_error("Failed to open journal: " + current_filename);

        std::cout << "[JOURNAL] Opened: " << current_filename << std::endl;

        // Update journal index file
        update_index();
    }

    void update_index()
    {
        std::ofstream idx(base_name + "_index.txt", std::ios::app);
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&t));
        
        idx << buf << " " << current_filename << std::endl;
        idx.flush();
    }

    void rotate_if_needed()
    {
        struct stat st;
        fstat(fd, &st);
        if((size_t)st.st_size > max_size)
        {
            std::cout << "[JOURNAL] Rotating (size: " << st.st_size << " bytes)" << std::endl;
            close();
            open_new_file();
        }
    }

    std::string timestamp_string()
    {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", gmtime(&t));
        return std::string(buf);
    }
};
