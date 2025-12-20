#include "replay/BinaryReplayEngine.hpp"
#include <iostream>

using namespace Chimera;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: replay <binary_log>\n";
        return 1;
    }

    BinaryReplayEngine r(argv[1]);
    r.run();
    return 0;
}
