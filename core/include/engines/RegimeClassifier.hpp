#pragma once

namespace chimera {

class RegimeClassifier {
public:
    RegimeClassifier();
    void update(int q);
    int quality() const;

private:
    int quality_;
};

} // namespace chimera
