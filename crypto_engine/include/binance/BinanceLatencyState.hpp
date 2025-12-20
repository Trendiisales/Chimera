#pragma once
#include "Latency.hpp"
#include "LowLatency.hpp"

namespace binance {

/* Hot latency state – cache aligned */
struct CACHE_ALIGN BinanceLatencyState {
    LatencyStats ws_to_book;
    LatencyStats snapshot_load;
};

}
