#pragma once
#include <string>

namespace Chimera {

class MetricsExporter {
public:
    explicit MetricsExporter(const std::string& prefix);

    void export_csv();
    void export_json();

private:
    std::string prefix_;
};

}
