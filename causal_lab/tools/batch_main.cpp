#include "ReplayEngine.hpp"
#include "RegimeStore.hpp"
#include <iostream>
#include <filesystem>

using namespace chimera_lab;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: chimera_batch <input_dir> <output.csv>\n";
        return 1;
    }

    RegimeStore store(argv[2]);
    
    std::cout << "Scanning " << argv[1] << " for event logs...\n";

    for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
        if (entry.path().extension() == ".bin") {
            std::cout << "Processing: " << entry.path().filename() << "\n";
            
            ReplayEngine replay(entry.path().string());
            
            replay.onFill([&](const EventHeader& h, const FillPayload& f) {
                AttributionResult r{}; // Simplified - would compute properly
                store.write(h.event_id, "BATCH", "AUTO", r, f.fill_qty * f.fill_price);
            });
            
            replay.run();
        }
    }

    std::cout << "Batch processing complete.\n";
    return 0;
}
