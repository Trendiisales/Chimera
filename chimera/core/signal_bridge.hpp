#ifndef SIGNAL_BRIDGE_HPP
#define SIGNAL_BRIDGE_HPP

#include <atomic>
#include <cstdint>

class SignalBridge {
public:
    void blockBTC(uint64_t until_ns) {
        uint64_t current = btc_block_until_.load();
        if (until_ns > current) {
            btc_block_until_.store(until_ns);
        }
    }

    void blockETH(uint64_t until_ns) {
        uint64_t current = eth_block_until_.load();
        if (until_ns > current) {
            eth_block_until_.store(until_ns);
        }
    }

    void blockFollowers(uint64_t until_ns) {
        uint64_t current = follower_block_until_.load();
        if (until_ns > current) {
            follower_block_until_.store(until_ns);
        }
    }

    bool btcBlocked(uint64_t now_ns) const {
        return now_ns < btc_block_until_.load();
    }

    bool ethBlocked(uint64_t now_ns) const {
        return now_ns < eth_block_until_.load();
    }

    bool followersBlocked(uint64_t now_ns) const {
        return now_ns < follower_block_until_.load();
    }

    uint64_t btcBlockRemaining(uint64_t now_ns) const {
        uint64_t until = btc_block_until_.load();
        return (now_ns < until) ? (until - now_ns) : 0;
    }

    uint64_t ethBlockRemaining(uint64_t now_ns) const {
        uint64_t until = eth_block_until_.load();
        return (now_ns < until) ? (until - now_ns) : 0;
    }

    void clearBlocks() {
        btc_block_until_.store(0);
        eth_block_until_.store(0);
        follower_block_until_.store(0);
    }

private:
    std::atomic<uint64_t> btc_block_until_{0};
    std::atomic<uint64_t> eth_block_until_{0};
    std::atomic<uint64_t> follower_block_until_{0};
};

#endif
