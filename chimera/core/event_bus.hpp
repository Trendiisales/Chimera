#ifndef EVENT_BUS_HPP
#define EVENT_BUS_HPP

#include <functional>
#include <vector>
#include <mutex>

template<typename T>
class EventBus {
public:
    using Handler = std::function<void(const T&)>;

    void subscribe(Handler h) {
        std::lock_guard<std::mutex> lock(mtx_);
        handlers_.push_back(std::move(h));
    }

    void publish(const T& event) {
        std::vector<Handler> snapshot;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            snapshot = handlers_;
        }
        for (auto& h : snapshot) {
            if (h) h(event);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        handlers_.clear();
    }

private:
    std::vector<Handler> handlers_;
    std::mutex mtx_;
};

#endif
