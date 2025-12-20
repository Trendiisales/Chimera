#include "replay/BinaryReplayEngine.hpp"
#include "logging/BinaryLog.hpp"
#include "binance/BinanceLogTypes.hpp"
#include "fix/FixLogTypes.hpp"
#include "replay/ReplayHooks.hpp"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

using namespace Chimera;

BinaryReplayEngine::BinaryReplayEngine(const std::string& path)
    : fd_(-1), size_(0), base_(nullptr) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    size_ = ::lseek(fd_, 0, SEEK_END);
    base_ = static_cast<const unsigned char*>(
        ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0)
    );
}

void BinaryReplayEngine::run() {
    size_t off = 0;

    while (off + sizeof(BinaryLogHeader) <= size_) {
        const BinaryLogHeader* h =
            reinterpret_cast<const BinaryLogHeader*>(base_ + off);

        off += sizeof(BinaryLogHeader);

        const void* payload = base_ + off;

        switch (h->type) {
            case static_cast<uint16_t>(LogRecordType::TICK): {
                if (h->venue == 1) { // BINANCE
                    const BinanceTickLog* t =
                        reinterpret_cast<const BinanceTickLog*>(payload);

                    replay_on_binance_tick(
                        t->ts_exchange,
                        t->bid,
                        t->ask,
                        t->bid_qty,
                        t->ask_qty
                    );
                }
                break;
            }

            case static_cast<uint16_t>(LogRecordType::EXECUTION): {
                if (h->venue == 2) { // FIX
                    const FixExecLog* e =
                        reinterpret_cast<const FixExecLog*>(payload);

                    replay_on_fix_execution(
                        e->cl_ord_id,
                        e->ts_exchange,
                        e->price,
                        e->qty,
                        e->side
                    );
                }
                break;
            }

            default:
                break;
        }

        off += h->size;
    }
}
