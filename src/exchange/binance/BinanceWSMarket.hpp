#pragma once
#include <string>
#include <atomic>
#include <unordered_map>
#include "runtime/Context.hpp"

namespace chimera {

class BinanceWSMarket {
public:
    // FIX: takes Context& so it can wire ctx_.queue.on_book_update()
    BinanceWSMarket(Context& ctx, const std::string& base);

    void run(std::atomic<bool>& running);

private:
    Context&    ctx_;
    std::string base_;

    // Per-symbol sequence tracker for bookTicker deduplication.
    // Member (not static) so it's cleared implicitly on reconnect (new object
    // scope per connect attempt doesn't apply here — same object, but we
    // clear() explicitly on reconnect entry).
    std::unordered_map<std::string, uint64_t> last_u_;

    void connect_loop(std::atomic<bool>& running);
    void parse_message(const std::string& msg);
};

} // namespace chimera
