#include "execution/OrderStateMachine.hpp"
#include <stdexcept>
#include <chrono>

using namespace chimera;

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void OrderStateMachine::on_new(const OrderRecord& rec) {
    std::lock_guard<std::mutex> lock(mtx_);
    OrderRecord r = rec;
    r.status = OrderStatus::NEW;
    r.last_update_ns = now_ns();
    orders_[rec.client_id] = r;
    // No exch_to_client_ entry yet — exchange_id is assigned on ACK
}

void OrderStateMachine::on_ack(const std::string& client_id, const std::string& exch_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) return;
    it->second.exchange_id = exch_id;
    it->second.status = OrderStatus::ACKED;
    it->second.last_update_ns = now_ns();

    // FIX 2.5: Populate secondary index on ack — this is when exchange_id is known.
    exch_to_client_[exch_id] = client_id;
}

void OrderStateMachine::on_fill(const std::string& exch_id, double filled_qty) {
    std::lock_guard<std::mutex> lock(mtx_);

    // FIX 2.5: O(1) lookup via secondary index instead of linear scan.
    auto idx_it = exch_to_client_.find(exch_id);
    if (idx_it == exch_to_client_.end()) return;

    auto it = orders_.find(idx_it->second);
    if (it == orders_.end()) return;

    it->second.qty -= filled_qty;
    it->second.status = it->second.qty <= 0.0 ? OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;
    it->second.last_update_ns = now_ns();

    // If fully filled, clean up the index entry
    if (it->second.status == OrderStatus::FILLED) {
        exch_to_client_.erase(idx_it);
    }
}

void OrderStateMachine::on_cancel(const std::string& exch_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    // FIX 2.5: O(1) lookup via secondary index instead of linear scan.
    auto idx_it = exch_to_client_.find(exch_id);
    if (idx_it == exch_to_client_.end()) return;

    auto it = orders_.find(idx_it->second);
    if (it == orders_.end()) return;

    it->second.status = OrderStatus::CANCELED;
    it->second.last_update_ns = now_ns();

    // Canceled — clean up the index entry
    exch_to_client_.erase(idx_it);
}

// ---------------------------------------------------------------------------
// Cancel by client_id — for orders still in NEW state (pre-ACK).
// These have no exch_to_client_ entry, so on_cancel() would miss them.
// Used by ExecutionRouter dedup cancel-replace when replacing an unfilled order.
// ---------------------------------------------------------------------------
void OrderStateMachine::on_cancel_by_client_id(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) return;

    // If it WAS acked (has an exchange_id), clean up the secondary index too.
    if (!it->second.exchange_id.empty()) {
        exch_to_client_.erase(it->second.exchange_id);
    }

    it->second.status = OrderStatus::CANCELED;
    it->second.last_update_ns = now_ns();
}

void OrderStateMachine::on_reject(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) return;
    it->second.status = OrderStatus::REJECTED;
    it->second.last_update_ns = now_ns();
    // No exchange_id on reject — nothing to clean in index
}

bool OrderStateMachine::is_open(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) return false;
    return it->second.status == OrderStatus::NEW ||
           it->second.status == OrderStatus::ACKED ||
           it->second.status == OrderStatus::PARTIALLY_FILLED;
}

OrderRecord OrderStateMachine::get(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) throw std::runtime_error("Order not found");
    return it->second;
}

// FIX 4.3: Dump all orders for snapshot persistence.
// Returns copy under lock — safe for concurrent access.
std::vector<OrderRecord> OrderStateMachine::dump_orders() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<OrderRecord> out;
    out.reserve(orders_.size());
    for (const auto& kv : orders_)
        out.push_back(kv.second);
    return out;
}

// FIX 4.3: Restore a single order from snapshot.
// Rebuilds both primary and secondary indices.
void OrderStateMachine::restore_order(const OrderRecord& rec) {
    std::lock_guard<std::mutex> lock(mtx_);
    orders_[rec.client_id] = rec;
    if (!rec.exchange_id.empty()) {
        exch_to_client_[rec.exchange_id] = rec.client_id;
    }
}

int OrderStateMachine::purge_terminal() {
    std::lock_guard<std::mutex> lock(mtx_);
    int count = 0;
    for (auto it = orders_.begin(); it != orders_.end(); ) {
        switch (it->second.status) {
            case OrderStatus::FILLED:
            case OrderStatus::CANCELED:
            case OrderStatus::REJECTED:
                it = orders_.erase(it);
                count++;
                break;
            default:
                ++it;
                break;
        }
    }
    return count;
}
