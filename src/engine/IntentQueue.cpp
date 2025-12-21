#include "engine/IntentQueue.hpp"

void IntentQueue::push(const Intent& intent) {
    std::lock_guard<std::mutex> lock(m_);
    q_.push(intent);
}

bool IntentQueue::try_pop(Intent& out) {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.empty()) return false;
    out = q_.front();
    q_.pop();
    return true;
}
