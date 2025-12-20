#pragma once

namespace Chimera {

// Simple bounded normalizer
class SignalNormalizer {
public:
    explicit SignalNormalizer(double clip);

    double norm(double v) const;

private:
    double clip_;
};

}
