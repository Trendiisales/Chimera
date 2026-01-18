#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include "chimera/Desk.hpp"

int main() {
    int fd = shm_open(SHM_BLOTTER, O_RDONLY, 0666);
    Blotter* b = (Blotter*)mmap(0, sizeof(Blotter), PROT_READ, MAP_SHARED, fd, 0);

    while (true) {
        uint32_t head = b->head;
        if (head > 0) {
            auto& o = b->orders[(head - 1) % MAX_ORDERS];
            std::cout << "[GUI] " << o.engine << " "
                      << o.symbol << " "
                      << o.price << " "
                      << o.qty << " "
                      << o.latency_us << "us\n";
        }
        usleep(500000);
    }
}
