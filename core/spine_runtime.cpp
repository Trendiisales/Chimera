#include "core/spine_runtime.hpp"

SpineRuntime::SpineRuntime(const std::string& bin,
                           const std::string& jsonl)
    : recorder(bin, jsonl) {}

uint64_t SpineRuntime::emit(EventType t,
                            const std::string& src,
                            const std::string& payload,
                            uint64_t parent) {
    uint64_t id = core.publish(t, src, payload, parent);
    auto snap = core.snapshot();
    recorder.record(snap.back());
    return id;
}

void SpineRuntime::flush() {
    recorder.flush();
}

EventSpine& SpineRuntime::spine() {
    return core;
}
