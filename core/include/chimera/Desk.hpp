#pragma once
#include <cstdint>
#include <cstring>

static const char* SHM_BLOTTER = "/chimera_blotter";
static const int MAX_ORDERS = 1024;
static const int SYMBOL_LEN = 16;

struct OrderRecord {
    uint64_t ts_ns;
    char engine[16];
    char symbol[SYMBOL_LEN];
    double price;
    double qty;
    int side;
    int venue;
    uint32_t latency_us;
};

struct Blotter {
    uint32_t head;
    OrderRecord orders[MAX_ORDERS];
};

inline void blotter_add(Blotter* b, const OrderRecord& r) {
    uint32_t idx = __atomic_fetch_add(&b->head, 1, __ATOMIC_RELAXED);
    b->orders[idx % MAX_ORDERS] = r;
}
