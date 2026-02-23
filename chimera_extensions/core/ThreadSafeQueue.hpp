#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

namespace chimera {
namespace core {

template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(value);
        }
        m_cv.notify_one();
    }

    bool try_pop(T& result) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) 
            return false;
        
        result = m_queue.front();
        m_queue.pop();
        return true;
    }

    T wait_and_pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        
        T value = m_queue.front();
        m_queue.pop();
        return value;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<T> m_queue;
};

} // namespace core
} // namespace chimera
