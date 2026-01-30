#pragma once
#include <functional>
#include <vector>

template<typename T>
class EventBus {
public:
    using Handler = std::function<void(const T&)>;
    void subscribe(Handler h) { handlers.push_back(h); }
    void publish(const T& e) {
        for (auto& h : handlers) h(e);
    }
private:
    std::vector<Handler> handlers;
};
