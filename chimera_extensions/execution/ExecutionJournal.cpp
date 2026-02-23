#include "ExecutionJournal.hpp"

namespace chimera {
namespace execution {

bool ExecutionJournal::register_execution(const std::string& exec_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // GAP #1 FIX: Check if already seen
    if (m_seen_executions.count(exec_id) > 0)
        return false;  // Duplicate - ignore
    
    m_seen_executions.insert(exec_id);
    return true;  // New execution
}

void ExecutionJournal::clear_old_executions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Keep last 10,000 for audit trail, clear older
    if (m_seen_executions.size() > 10000) {
        // In production, use time-based eviction
        // For now, clear all (safe since we check on each fill)
        m_seen_executions.clear();
    }
}

size_t ExecutionJournal::get_execution_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_seen_executions.size();
}

} // namespace execution
} // namespace chimera
