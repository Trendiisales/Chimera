#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>

namespace chimera {

enum class OrderStatus {
    NEW, ACKED, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED
};

struct OrderRecord {
    std::string client_id;
    std::string exchange_id;
    std::string symbol;
    double price;
    double qty;
    OrderStatus status;
    uint64_t last_update_ns;
};

class OrderStateMachine {
public:
    void on_new(const OrderRecord& rec);
    void on_ack(const std::string& client_id, const std::string& exch_id);
    void on_fill(const std::string& exch_id, double filled_qty);
    void on_cancel(const std::string& exch_id);

    // ---------------------------------------------------------------------------
    // Cancel by client_id — used by ExecutionRouter dedup cancel-replace path.
    // The order is still in NEW state (never acked), so exch_to_client_ has no
    // entry. on_cancel() would silently miss it. This method goes direct to
    // the primary index.
    // ---------------------------------------------------------------------------
    void on_cancel_by_client_id(const std::string& client_id);

    void on_reject(const std::string& client_id);

    bool is_open(const std::string& client_id);
    OrderRecord get(const std::string& client_id);

    // FIX 4.3: Snapshot support — persist and restore open orders across restarts.
    // Previously: snapshot saved positions and queue books but not orders.
    // On crash, orders in flight were lost. Local positions could reflect fills
    // with no order trail. For live mode this means phantom positions.
    std::vector<OrderRecord> dump_orders() const;
    void restore_order(const OrderRecord& rec);

    // ---------------------------------------------------------------------------
    // Purge terminal orders from orders_ map.
    // FILLED, CANCELED, REJECTED records accumulate forever otherwise.
    // Called periodically from ExecutionRouter::poll() to prevent unbounded growth.
    // Returns number of records purged.
    // ---------------------------------------------------------------------------
    int purge_terminal();

private:
    // Primary index: client_id → OrderRecord
    std::unordered_map<std::string, OrderRecord> orders_;

    // FIX 2.5: Secondary index for O(1) fill/cancel lookup by exchange_id.
    // Previously: on_fill() and on_cancel() did linear scan over all orders
    // searching for matching exchange_id. At HFT volumes this becomes a hot-path
    // bottleneck holding the mutex for the entire scan.
    // Now: exch_to_client_ maps exchange_id → client_id. Updated in on_ack()
    // when exchange assigns the exchange_id. on_fill/on_cancel do O(1) lookup.
    std::unordered_map<std::string, std::string> exch_to_client_;

    mutable std::mutex mtx_;
};

} // namespace chimera
