#pragma once

#include "chimera/causal/recorder.hpp"
#include <memory>
#include <mutex>

namespace chimera::causal {

class GlobalRecorder {
public:
    static GlobalRecorder& instance() {
        static GlobalRecorder inst;
        return inst;
    }
    
    Recorder& get() {
        return *recorder_;
    }
    
    void initialize(const std::string& base_path) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!recorder_) {
            recorder_ = std::make_unique<Recorder>(base_path);
        }
    }
    
    bool is_initialized() const {
        return recorder_ != nullptr;
    }

private:
    GlobalRecorder() = default;
    ~GlobalRecorder() = default;
    
    GlobalRecorder(const GlobalRecorder&) = delete;
    GlobalRecorder& operator=(const GlobalRecorder&) = delete;
    
    std::unique_ptr<Recorder> recorder_;
    std::mutex mtx_;
};

// Convenience function
inline Recorder& global_recorder() {
    return GlobalRecorder::instance().get();
}

}
