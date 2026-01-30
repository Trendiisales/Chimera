#include "core/spine_recorder.hpp"
#include <cstring>

SpineRecorder::SpineRecorder(const std::string& bin_path,
                             const std::string& jsonl_path) {
    bin.open(bin_path, std::ios::binary | std::ios::out);
    jsonl.open(jsonl_path, std::ios::out);
}

void SpineRecorder::write_bin(const SpineEvent& e) {
    uint64_t len = e.payload.size();
    bin.write(reinterpret_cast<const char*>(&e.id), sizeof(e.id));
    bin.write(reinterpret_cast<const char*>(&e.ts_ns), sizeof(e.ts_ns));
    uint32_t t = static_cast<uint32_t>(e.type);
    bin.write(reinterpret_cast<const char*>(&t), sizeof(t));
    bin.write(reinterpret_cast<const char*>(&len), sizeof(len));
    bin.write(e.payload.data(), len);
}

void SpineRecorder::write_json(const SpineEvent& e) {
    jsonl << "{";
    jsonl << "\"id\":" << e.id << ",";
    jsonl << "\"ts_ns\":" << e.ts_ns << ",";
    jsonl << "\"type\":" << static_cast<int>(e.type) << ",";
    jsonl << "\"source\":\"" << e.source << "\",";
    jsonl << "\"payload\":\"";

    for (char c : e.payload) {
        if (c == '"' || c == '\\')
            jsonl << '\\';
        jsonl << c;
    }

    jsonl << "\"}\n";
}

void SpineRecorder::record(const SpineEvent& e) {
    write_bin(e);
    write_json(e);
}

void SpineRecorder::flush() {
    bin.flush();
    jsonl.flush();
}
