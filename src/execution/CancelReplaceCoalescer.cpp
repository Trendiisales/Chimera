#include "execution/CancelReplaceCoalescer.hpp"

using namespace chimera;

bool CancelReplaceCoalescer::submit(const std::string& id, const CoalesceOrder& ord) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_[id] = ord;
    return true;
}

bool CancelReplaceCoalescer::get(const std::string& id, CoalesceOrder& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = pending_.find(id);
    if (it == pending_.end()) return false;
    out = it->second;
    return true;
}

void CancelReplaceCoalescer::clear(const std::string& id) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.erase(id);
}

bool CancelReplaceCoalescer::find_by_engine_symbol(
    const std::string& engine_id,
    const std::string& symbol,
    std::string& out_client_id,
    CoalesceOrder& out_ord)
{
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& [cid, ord] : pending_) {
        if (ord.engine_id == engine_id && ord.symbol == symbol) {
            out_client_id = cid;
            out_ord       = ord;
            return true;
        }
    }
    return false;
}

// FIX 2.1: Return snapshot of all pending client_ids under lock.
// ExecutionRouter::poll() uses this to iterate pending orders in shadow mode.
std::vector<std::string> CancelReplaceCoalescer::pending_keys() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> keys;
    keys.reserve(pending_.size());
    for (const auto& kv : pending_)
        keys.push_back(kv.first);
    return keys;
}
