#pragma once

#include <unordered_set>
#include <string>
#include <mutex>

namespace chimera {
namespace execution {

// GAP #1 FIX: Prevents duplicate fill callbacks from corrupting allocator
class ExecutionJournal {
public:
    ExecutionJournal() = default;
    
    // Returns true if execution is new, false if duplicate
    bool register_execution(const std::string& exec_id);
    
    // For cleanup (optional - can keep all for audit trail)
    void clear_old_executions();
    
    size_t get_execution_count() const;

private:
    std::unordered_set<std::string> m_seen_executions;
    mutable std::mutex m_mutex;
};

} // namespace execution
} // namespace chimera
